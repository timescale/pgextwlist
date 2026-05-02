/* PostgreSQL Extension WhiteList -- Dimitri Fontaine
 *
 * Author: Dimitri Fontaine <dimitri@2ndQuadrant.fr>
 * Licence: PostgreSQL
 * Copyright Dimitri Fontaine, 2011-2013
 *
 * For a description of the features see the README.md file from the same
 * distribution.
 */

#include <stdio.h>
#include <unistd.h>
#include "postgres.h"

#include "pgextwlist.h"
#include "utils.h"

#include "access/genam.h"
#include "access/heapam.h"
#include "access/htup_details.h"
#include "access/xact.h"
#include "catalog/dependency.h"
#include "catalog/indexing.h"
#include "catalog/objectaccess.h"
#include "catalog/pg_class.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_auth_members.h"
#include "catalog/pg_authid.h"
#include "catalog/pg_database.h"
#include "catalog/pg_db_role_setting.h"
#include "catalog/namespace.h"
#include "catalog/pg_namespace.h"
#include "commands/extension.h"
#include "commands/comment.h"
#include "commands/dbcommands.h"
#include "commands/seclabel.h"
#include "commands/user.h"
#if PG_MAJOR_VERSION >= 1000
#include "common/md5.h"
#else
#include "libpq/md5.h"
#endif
#include "funcapi.h"
#include "miscadmin.h"
#include "storage/lmgr.h"
#include "tcop/utility.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/inval.h"
#include "utils/lsyscache.h"
#include "utils/catcache.h"
#include "utils/syscache.h"
#include "utils/timestamp.h"
#if PG_MAJOR_VERSION < 1200
#include "utils/tqual.h"
#endif
#if PG_MAJOR_VERSION >= 1000
#include "utils/varlena.h"
#endif
#if PG_MAJOR_VERSION >= 1200
#include "access/table.h"
#else
#define table_open(r, l) heap_open(r, l)
#define table_close(r, l) heap_close(r, l)
#endif

/*
 * This code has only been tested with PostgreSQL 9.1.
 *
 * It should be "deprecated" in 9.2 and following thanks to command triggers,
 * and the extension mechanism it works with didn't exist before 9.1.
 */
PG_MODULE_MAGIC;

char *extwlist_extensions = NULL;
char *extwlist_custom_path = NULL;
bool extwlist_extname_from_filename = false;

static ProcessUtility_hook_type prev_ProcessUtility = NULL;

void		_PG_init(void);
void		_PG_fini(void);

#if PG_MAJOR_VERSION < 903
#define PROCESS_UTILITY_PROTO_ARGS Node *parsetree, const char *queryString,  \
									ParamListInfo params, bool isTopLevel,    \
									DestReceiver *dest, char *completionTag

#define PROCESS_UTILITY_ARGS parsetree, queryString, params, \
                              isTopLevel, dest, completionTag
#elif PG_MAJOR_VERSION < 1000
#define PROCESS_UTILITY_PROTO_ARGS Node *parsetree,                    \
										const char *queryString,       \
										ProcessUtilityContext context, \
										ParamListInfo params,          \
										DestReceiver *dest,            \
										char *completionTag

#define PROCESS_UTILITY_ARGS parsetree, queryString, context, \
                              params, dest, completionTag
#elif PG_MAJOR_VERSION < 1300
#define PROCESS_UTILITY_PROTO_ARGS PlannedStmt *pstmt,                    \
										const char *queryString,       \
										ProcessUtilityContext context, \
										ParamListInfo params,          \
										QueryEnvironment *queryEnv,    \
										DestReceiver *dest,            \
										char *completionTag

#define PROCESS_UTILITY_ARGS pstmt, queryString, context, \
                              params, queryEnv, dest, completionTag
#elif PG_MAJOR_VERSION < 1400
#define PROCESS_UTILITY_PROTO_ARGS PlannedStmt *pstmt,                    \
										const char *queryString,       \
										ProcessUtilityContext context, \
										ParamListInfo params,          \
										QueryEnvironment *queryEnv,    \
										DestReceiver *dest,            \
										QueryCompletion *qc
#define PROCESS_UTILITY_ARGS pstmt, queryString, context, \
                              params, queryEnv, dest, qc
#else
#define PROCESS_UTILITY_PROTO_ARGS PlannedStmt *pstmt,                    \
										const char *queryString,       \
										bool readOnlyTree,             \
										ProcessUtilityContext context, \
										ParamListInfo params,          \
										QueryEnvironment *queryEnv,    \
										DestReceiver *dest,            \
										QueryCompletion *qc
#define PROCESS_UTILITY_ARGS pstmt, queryString, readOnlyTree, context, \
                              params, queryEnv, dest, qc
#endif	/* PG_MAJOR_VERSION */

#define EREPORT_EXTENSION_IS_NOT_WHITELISTED(op)						\
        ereport(ERROR,                                                  \
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),              \
                 errmsg("extension \"%s\" is not whitelisted", name),   \
                 errdetail("%s the extension \"%s\" failed, "           \
                           "because it is not on the whitelist of "     \
                           "user-installable extensions.", op, name),	\
                 errhint("Your system administrator has allowed users " \
                         "to install certain extensions. "              \
						 "See: SHOW extwlist.extensions;")));


static void extwlist_ProcessUtility(PROCESS_UTILITY_PROTO_ARGS);
static void call_ProcessUtility(PROCESS_UTILITY_PROTO_ARGS,
								const char *name,
								const char *schema,
								const char *old_version,
								const char *new_version,
								const char *action);
static void call_RawProcessUtility(PROCESS_UTILITY_PROTO_ARGS);

/*
 * _PG_init()			- library load-time initialization
 *
 * DO NOT make this static nor change its name!
 *
 * Init the module, all we have to do here is getting our GUC
 */
void
_PG_init(void)
{
	DefineCustomStringVariable("extwlist.extensions",
							   "List of extensions that are whitelisted",
							   "Separated by comma",
							   &extwlist_extensions,
							   "",
							   PGC_SUSET,
							   GUC_NOT_IN_SAMPLE,
							   NULL,
							   NULL,
							   NULL);

	DefineCustomStringVariable("extwlist.custom_path",
							   "Directory where to load custom scripts from",
							   "",
							   &extwlist_custom_path,
							   "",
							   PGC_SUSET,
							   GUC_NOT_IN_SAMPLE,
							   NULL,
							   NULL,
							   NULL);

    DefineCustomBoolVariable("extwlist.extname_from_filename",
                               "Flag allowing a lookup of extension name in custom script filename",
                               "",
                               &extwlist_extname_from_filename,
                               false,
                               PGC_SUSET,
                               GUC_NOT_IN_SAMPLE,
                               NULL,
                               NULL,
                               NULL);

    EmitWarningsOnPlaceholders("extwlist");

	prev_ProcessUtility = ProcessUtility_hook;
	ProcessUtility_hook = extwlist_ProcessUtility;
}

/*
 * Extension Whitelisting includes mechanisms to run custom scripts before and
 * after the extension's provided script.
 *
 * We lookup scripts at the following places and run them when they exist:
 *
 *  ${extwlist_custom_path}/${extname}/${when}--${version}.sql (create)
 *  ${extwlist_custom_path}/${extname}/${when}--${oldversion}--${newversion}.sql (upgrade)
 *  ${extwlist_custom_path}/${extname}/${when}-${action}--${version}.sql (rest)
 *  ${extwlist_custom_path}/${extname}/${when}-${action}.sql (all actions)
 *
 * - action is expected to be one of "create", "update", "comment", or "drop"
 * - when   is expected to be either "before" or "after"
 *
 * if extwlist.extname_from_filename is set, then custom scripts must have the following format:
 *
 *  ${extwlist_custom_path}/${extname}--${when}--${version}.sql (create)
 *  ${extwlist_custom_path}/${extname}--${when}--${oldversion}--${newversion}.sql (upgrade)
 *  ${extwlist_custom_path}/${extname}--${when}-${action}--${version}.sql (rest)
 *  ${extwlist_custom_path}/${extname}--${when}-${action}.sql (all actions)
 *
 * We don't validation the extension's name before building the scripts path
 * here because the extension name we are dealing with must have already been
 * added to the whitelist, which should be enough of a validation step.
 */
static void
call_extension_scripts(const char *extname,
					   const char *schema,
					   const char *action,
					   const char *when,
					   const char *from_version,
					   const char *version)
{
	char *specific_custom_script;
	char *generic_custom_script;

	if (version)
	{
		specific_custom_script =
			get_specific_custom_script_filename(extname, when,
												from_version, version);

		elog(DEBUG1, "Considering custom script \"%s\"", specific_custom_script);

		if (access(specific_custom_script, F_OK) == 0)
		{
			execute_custom_script(specific_custom_script, schema);
			return; /* skip generic script */
		}
	}

	generic_custom_script =
		get_generic_custom_script_filename(extname, action, when);

	elog(DEBUG1, "Considering custom script \"%s\"", generic_custom_script);

	if (access(generic_custom_script, F_OK) == 0)
		execute_custom_script(generic_custom_script, schema);
}

/*
 * We do not allow extensions to be created or updated if there are temporary
 * objects in the session, because those objects could shadow objects accessed
 * by the extension's script, leading to security vulnerabilities for extensions
 * that do not schema-qualify their objects or lock down their search_path.
 */
static void
pg_temp_is_empty(const char *name)
{
	Oid			temp_namespace;
	Relation	rel;
	ScanKeyData key[1];
	SysScanDesc scan;

	temp_namespace = LookupExplicitNamespace("pg_temp", true);
	if (!OidIsValid(temp_namespace))
		return;

	rel = table_open(RelationRelationId, AccessShareLock);

	ScanKeyInit(&key[0],
				Anum_pg_class_relnamespace,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(temp_namespace));

	scan = systable_beginscan(rel, InvalidOid, false,
							  NULL, 1, key);

	if (HeapTupleIsValid(systable_getnext(scan)))
		ereport(ERROR, (errcode(ERRCODE_OPERATOR_INTERVENTION),
			errmsg("extension \"%s\" cannot be installed with temporary objects in the session", name),
			errhint("Start a new session and execute CREATE EXTENSION as the first command. "
				"Make sure to pass the \"-X\" flag to psql.")));

	systable_endscan(scan);
	table_close(rel, AccessShareLock);
}

/*
 * Check that the extension's target schema does not contain relations whose
 * names match relations in pg_catalog.
 */
static void
no_relation_shadows(const char *name, const char *schema, Oid ext_namespace)
{
	Relation	rel;
	ScanKeyData key[1];
	SysScanDesc scan;
	HeapTuple	tuple;

	rel = table_open(RelationRelationId, AccessShareLock);

	ScanKeyInit(&key[0],
				Anum_pg_class_relnamespace,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(ext_namespace));

	scan = systable_beginscan(rel, InvalidOid, false,
							  NULL, 1, key);

	while (HeapTupleIsValid(tuple = systable_getnext(scan)))
	{
		Form_pg_class classForm = (Form_pg_class) GETSTRUCT(tuple);
		const char *relname = NameStr(classForm->relname);
		Oid			catalog_oid;

		catalog_oid = get_relname_relid(relname, PG_CATALOG_NAMESPACE);
		if (!OidIsValid(catalog_oid))
			continue;

		systable_endscan(scan);
		table_close(rel, AccessShareLock);

		ereport(ERROR,
				(errcode(ERRCODE_OPERATOR_INTERVENTION),
				 errmsg("extension \"%s\" cannot be installed because "
						"\"%s\".\"%s\" shadows a pg_catalog object",
						name, schema, relname),
				 errhint("Drop the shadowing object and try again.")));
	}

	systable_endscan(scan);
	table_close(rel, AccessShareLock);
}

/*
 * Check that the extension's target schema does not contain functions whose
 * names match functions in pg_catalog.
 *
 * If variadic_catalog_only is true, the check only fires when the shadowed
 * pg_catalog function is itself variadic — regardless of whether the
 * shadowing user function is variadic. Variadic catalog shadows are the
 * easiest class to exploit, so blocking them at runtime is the highest-
 * value protection: it shields users still on older timescaledb versions
 * (whose own code may not yet defend against this) from exploitation
 * attempts. Non-variadic shadows are also potentially exploitable but
 * are caught by timescaledb's own CI on current versions, so we accept
 * that residual risk in exchange for permitting legitimate shadowing
 * (e.g. installing aggregate functions for additional data types).
 */
static void
no_function_shadows(const char *name, const char *schema, Oid ext_namespace,
					bool variadic_catalog_only)
{
	Relation	rel;
	ScanKeyData key[1];
	SysScanDesc scan;
	HeapTuple	tuple;

	rel = table_open(ProcedureRelationId, AccessShareLock);

	ScanKeyInit(&key[0],
				Anum_pg_proc_pronamespace,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(ext_namespace));

	scan = systable_beginscan(rel, InvalidOid, false,
							  NULL, 1, key);

	while (HeapTupleIsValid(tuple = systable_getnext(scan)))
	{
		Form_pg_proc procForm = (Form_pg_proc) GETSTRUCT(tuple);
		const char *proname = NameStr(procForm->proname);
#if PG_MAJOR_VERSION >= 1200
		Oid			funcOid = procForm->oid;
#else
		Oid			funcOid = HeapTupleGetOid(tuple);
#endif
		CatCList   *catlist;
		int			i;

		/* Functions owned by an extension are legitimate, skip them */
		if (OidIsValid(getExtensionOfObject(ProcedureRelationId, funcOid)))
			continue;

		catlist = SearchSysCacheList1(PROCNAMEARGSNSP,
									  CStringGetDatum(proname));

		for (i = 0; i < catlist->n_members; i++)
		{
			HeapTuple	htup = &catlist->members[i]->tuple;
			Form_pg_proc catalogForm = (Form_pg_proc) GETSTRUCT(htup);

			if (catalogForm->pronamespace != PG_CATALOG_NAMESPACE)
				continue;

			if (variadic_catalog_only &&
				!OidIsValid(catalogForm->provariadic))
				continue;

			ReleaseSysCacheList(catlist);
			systable_endscan(scan);
			table_close(rel, AccessShareLock);

			ereport(ERROR,
					(errcode(ERRCODE_OPERATOR_INTERVENTION),
					 errmsg("extension \"%s\" cannot be installed because "
							"\"%s\".\"%s\"() shadows a pg_catalog function",
							name, schema, proname),
					 errhint("Drop the shadowing function and try again.")));
		}

		ReleaseSysCacheList(catlist);
	}

	systable_endscan(scan);
	table_close(rel, AccessShareLock);
}

static void
check_environment(const char *name, const char *schema)
{
	Oid			ext_namespace;

	pg_temp_is_empty(name);

	ext_namespace = LookupExplicitNamespace(schema, true);
	if (!OidIsValid(ext_namespace))
		return;

	/* If the extension targets pg_catalog itself, no shadowing is possible */
	if (ext_namespace == PG_CATALOG_NAMESPACE)
		return;

	no_relation_shadows(name, schema, ext_namespace);

	/*
	 * For timescaledb, only block shadows of variadic pg_catalog
	 * functions. Variadic shadows are the easiest to exploit, and
	 * blocking them here protects users on older timescaledb versions
	 * whose own code may not yet defend against this. Non-variadic
	 * shadows are also potentially exploitable but are caught by
	 * timescaledb's own CI on current versions, which makes the residual
	 * risk acceptable in exchange for permitting legitimate shadowing
	 * (e.g. aggregate functions on additional data types).
	 */
	no_function_shadows(name, schema, ext_namespace,
						strcmp(name, "timescaledb") == 0);
}

static bool
extension_is_whitelisted(const char *name)
{
	bool        whitelisted = false;
	char       *rawnames = pstrdup(extwlist_extensions);
	List       *extensions;
	ListCell   *lc;

	if (!SplitIdentifierString(rawnames, ',', &extensions))
	{
		/* syntax error in extension name list */
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("parameter \"extwlist.extensions\" must be a list of extension names")));
	}
	foreach(lc, extensions)
	{
		char *curext = (char *) lfirst(lc);

		if (strcmp(name, curext) == 0)
		{
			whitelisted = true;
			break;
		}
	}
	return whitelisted;
}

/*
 * ProcessUtility hook
 */
static void
extwlist_ProcessUtility(PROCESS_UTILITY_PROTO_ARGS)
{
	char	*name = NULL;
	char    *schema = NULL;
	char    *old_version = NULL;
	char    *new_version = NULL;

#if PG_MAJOR_VERSION >= 1000
	Node       *parsetree = pstmt->utilityStmt;
#endif

	/*
	 * Don't try to make life hard for our friendly superusers. Also, if
	 * a valid transaction is not ongoing then return early.
	 */
	if (!IsTransactionState() || superuser())
	{
		call_RawProcessUtility(PROCESS_UTILITY_ARGS);
		return;
	}

	switch (nodeTag(parsetree))
	{
		case T_CreateExtensionStmt:
		{
			CreateExtensionStmt *stmt = (CreateExtensionStmt *)parsetree;
			name = stmt->extname;
			fill_in_extension_properties(name, stmt->options,
										 &schema, &old_version, &new_version);

			if (extension_is_whitelisted(name))
			{
        check_environment(name, schema);
				call_ProcessUtility(PROCESS_UTILITY_ARGS,
									name, schema,
									old_version, new_version, "create");
				return;
			}
			break;
		}

		case T_AlterExtensionStmt:
		{
			AlterExtensionStmt *stmt = (AlterExtensionStmt *)parsetree;
			name = stmt->extname;
			fill_in_extension_properties(name, stmt->options,
										 &schema, &old_version, &new_version);

			/* fetch old_version from the catalogs, actually */
			old_version = get_extension_current_version(name);

			if (extension_is_whitelisted(name))
			{
				call_ProcessUtility(PROCESS_UTILITY_ARGS,
									name, schema,
									old_version, new_version, "update");
				return;
			}
			break;
		}

		case T_DropStmt:
			if (((DropStmt *)parsetree)->removeType == OBJECT_EXTENSION)
			{
				/* DROP EXTENSION can target several of them at once */
				bool all_in_whitelist = true;
				ListCell *lc;

				foreach(lc, ((DropStmt *)parsetree)->objects)
				{
					/*
					 * For deconstructing the object list into actual names,
					 * see the get_object_address_unqualified() function in
					 * src/backend/catalog/objectaddress.c
					 */
					bool whitelisted = false;
					List *objname = lfirst(lc);
#if PG_MAJOR_VERSION < 1000
					name = strVal(linitial(objname));
#elif PG_MAJOR_VERSION < 1500
					name = strVal((Value *) objname);
#else
					name = strVal(castNode(String, objname));
#endif

					whitelisted = extension_is_whitelisted(name);
					all_in_whitelist = all_in_whitelist && whitelisted;
				}

				/*
				 * If we have a mix of whitelisted and non-whitelisted
				 * extensions in a single DROP EXTENSION command, better play
				 * safe and do the DROP without superpowers.
				 *
				 * So we only give superpowers when all extensions are in the
				 * whitelist.
				 */
				if (all_in_whitelist)
				{
					call_ProcessUtility(PROCESS_UTILITY_ARGS,
										NULL, "", /* schema must not be NULL */
										NULL, NULL, "drop");
					return;
				}
			}
			break;

		case T_CommentStmt:
		{
			CommentStmt* stmt = (CommentStmt *)parsetree;
			if (stmt->objtype == OBJECT_EXTENSION)
			{
#if PG_MAJOR_VERSION < 1000
				name = strVal(linitial(stmt->objname));
#elif PG_MAJOR_VERSION < 1500
				name = strVal((Value *) stmt->object);
#else
				name = strVal(castNode(String, stmt->object));
#endif

				if (extension_is_whitelisted(name))
				{
					call_ProcessUtility(PROCESS_UTILITY_ARGS,
										name, "", /* schema must not be NULL */
										NULL, NULL, "comment");
					return;
				}
			}
			break;
		}
			/* We intentionally don't support that command. */
		case T_AlterExtensionContentsStmt:
		default:
			break;
	}

	/*
	 * We can only fall here if we don't want to support the command, so pass
	 * control over to the usual processing.
	 */
	call_RawProcessUtility(PROCESS_UTILITY_ARGS);
}

/*
 * Change current user and security context as if running a SECURITY DEFINER
 * procedure owned by a superuser, hard coded as the bootstrap user.
 */
static void
call_ProcessUtility(PROCESS_UTILITY_PROTO_ARGS,
					const char *name,
					const char *schema,
					const char *old_version,
					const char *new_version,
					const char *action)
{
	Oid			save_userid;
	int			save_sec_context;

	GetUserIdAndSecContext(&save_userid, &save_sec_context);

	SetUserIdAndSecContext(BOOTSTRAP_SUPERUSERID,
						   save_sec_context
						   | SECURITY_LOCAL_USERID_CHANGE
						   | SECURITY_RESTRICTED_OPERATION);

	if (action)
	{
		/* "drop extension" can list several extensions, walk them here */
		if (strcmp(action, "drop") == 0)
		{
			Node   *parsetree = pstmt->utilityStmt;
			ListCell *lc;
			char   *name = NULL;

			foreach(lc, ((DropStmt *)parsetree)->objects)
			{
				List *objname = lfirst(lc);
#if PG_MAJOR_VERSION < 1000
				name = strVal(linitial(objname));
#elif PG_MAJOR_VERSION < 1500
				name = strVal((Value *) objname);
#else
				name = strVal(castNode(String, objname));
#endif
				call_extension_scripts(name, schema, action,
									   "before", old_version, new_version);
			}
		}
		else
			call_extension_scripts(name, schema, action,
								   "before", old_version, new_version);
	}

	call_RawProcessUtility(PROCESS_UTILITY_ARGS);

	if (action)
	{
		if (strcmp(action, "drop") == 0)
		{
			Node   *parsetree = pstmt->utilityStmt;
			ListCell *lc;
			char   *name = NULL;

			foreach(lc, ((DropStmt *)parsetree)->objects)
			{
				List *objname = lfirst(lc);
#if PG_MAJOR_VERSION < 1000
				name = strVal(linitial(objname));
#elif PG_MAJOR_VERSION < 1500
				name = strVal((Value *) objname);
#else
				name = strVal(castNode(String, objname));
#endif
				call_extension_scripts(name, schema, action,
									   "after", old_version, new_version);
			}
		}
		else
			call_extension_scripts(name, schema, action,
								   "after", old_version, new_version);
	}

	SetUserIdAndSecContext(save_userid, save_sec_context);
}

static void
call_RawProcessUtility(PROCESS_UTILITY_PROTO_ARGS)
{
	if (prev_ProcessUtility)
		prev_ProcessUtility(PROCESS_UTILITY_ARGS);
	else
		standard_ProcessUtility(PROCESS_UTILITY_ARGS);
}

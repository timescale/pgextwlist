
SET ROLE admin;

CREATE SCHEMA custom;

-- verify baseline: whitelisted extension can be created normally
CREATE EXTENSION citext SCHEMA public;
DROP EXTENSION citext;

-- create a table in public that shadows pg_catalog.pg_type
CREATE TABLE public.pg_type(id int);

-- whitelisted extension should be blocked when schema has catalog shadows
CREATE EXTENSION citext;
CREATE EXTENSION citext SCHEMA public;

-- installation in custom should succeed since it doesn't shadow pg_catalog
CREATE EXTENSION citext SCHEMA custom;
DROP EXTENSION citext;

-- clean up shadow table
DROP TABLE public.pg_type;

-- repeat with a view that shadows pg_catalog.pg_type
CREATE VIEW public.pg_type AS SELECT * FROM pg_catalog.pg_type;

-- whitelisted extension should be blocked when schema has catalog shadows
CREATE EXTENSION citext;
CREATE EXTENSION citext SCHEMA public;

-- installation in custom should succeed since it doesn't shadow pg_catalog
CREATE EXTENSION citext SCHEMA custom;
DROP EXTENSION citext;

DROP VIEW public.pg_type;

-- now with shadow removed, creation should succeed again
CREATE EXTENSION citext;
DROP EXTENSION citext;
CREATE EXTENSION citext SCHEMA public;
DROP EXTENSION citext;

-- repeat the same but with functions in custom schema instead of public

-- create a table in custom that shadows pg_catalog.pg_type
CREATE TABLE custom.pg_type(id int);

-- whitelisted extension should be blocked when schema has catalog shadows
CREATE EXTENSION citext SCHEMA custom;

-- installation in public should succeed since it doesn't shadow pg_catalog
CREATE EXTENSION citext;
DROP EXTENSION citext;
CREATE EXTENSION citext SCHEMA public;
DROP EXTENSION citext;

-- clean up shadow table
DROP TABLE custom.pg_type;

-- repeat with a view that shadows pg_catalog.pg_type
CREATE VIEW custom.pg_type AS SELECT * FROM pg_catalog.pg_type;

-- installation in public should succeed since it doesn't shadow pg_catalog
CREATE EXTENSION citext;
DROP EXTENSION citext;
CREATE EXTENSION citext SCHEMA public;
DROP EXTENSION citext;

-- whitelisted extension should be blocked when schema has catalog shadows
CREATE EXTENSION citext SCHEMA custom;

DROP VIEW custom.pg_type;

-- create a function in public that shadows pg_catalog.format()
CREATE FUNCTION public.format(text,text,text) RETURNS text
    LANGUAGE sql AS $$ SELECT $1 $$;

-- whitelisted extension should be blocked by function shadow
CREATE EXTENSION citext;
CREATE EXTENSION citext SCHEMA public;

-- installation in custom should succeed since it doesn't shadow pg_catalog
CREATE EXTENSION citext SCHEMA custom;
DROP EXTENSION citext;

-- clean up shadow function
DROP FUNCTION public.format(text,text,text);

-- now with shadow removed, creation should succeed again
CREATE EXTENSION citext;
DROP EXTENSION citext;
CREATE EXTENSION citext SCHEMA public;
DROP EXTENSION citext;

-- repeat the same but with function in custom schema instead of public

-- create a function in public that shadows pg_catalog.format()
CREATE FUNCTION custom.format(text,text,text) RETURNS text
    LANGUAGE sql AS $$ SELECT $1 $$;

-- whitelisted extension should succeed in public since it doesn't shadow pg_catalog
CREATE EXTENSION citext;
DROP EXTENSION citext;
CREATE EXTENSION citext SCHEMA public;
DROP EXTENSION citext;

-- installation in custom should be blocked by function shadow
CREATE EXTENSION citext SCHEMA custom;

-- clean up shadow function
DROP FUNCTION custom.format(text,text,text);

-- now with shadow removed, creation should succeed again
CREATE EXTENSION citext SCHEMA custom;
DROP EXTENSION citext;


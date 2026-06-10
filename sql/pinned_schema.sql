
SET ROLE admin;

-- pinned_stub pins "schema = pinned_stub" in its control file. A
-- precreated pinned schema must be owned by a superuser, otherwise
-- CREATE EXTENSION and ALTER EXTENSION UPDATE are blocked.

-- fresh install: the schema does not exist and is created during
-- CREATE EXTENSION, owned by the bootstrap superuser.
CREATE EXTENSION pinned_stub;
SELECT r.rolsuper AS schema_owner_is_superuser
  FROM pg_namespace n JOIN pg_roles r ON r.oid = n.nspowner
 WHERE n.nspname = 'pinned_stub';
DROP EXTENSION pinned_stub;

-- DROP EXTENSION leaves the schema behind, superuser-owned;
-- reinstalling into it succeeds.
CREATE EXTENSION pinned_stub;
DROP EXTENSION pinned_stub;

RESET ROLE;
DROP SCHEMA pinned_stub;
SET ROLE admin;

-- a precreated schema owned by a non-superuser blocks installation.
CREATE SCHEMA pinned_stub;
CREATE EXTENSION pinned_stub;

-- an explicit SCHEMA clause naming the pinned schema is blocked the same way.
CREATE EXTENSION pinned_stub SCHEMA pinned_stub;
DROP SCHEMA pinned_stub;

-- an explicit SCHEMA clause that does not match the pinned schema is
-- rejected by core, not by the ownership check.
CREATE SCHEMA not_pinned;
CREATE EXTENSION pinned_stub SCHEMA not_pinned;
DROP SCHEMA not_pinned;

-- the ownership requirement is scoped to extensions that pin their schema.
-- timescaledb does not pin one, so it may be installed into a schema owned
-- by a non-superuser (the admin role); the shadow checks still apply.
CREATE SCHEMA unpinned_target;
CREATE EXTENSION timescaledb VERSION '0.0.0' SCHEMA unpinned_target;
DROP EXTENSION timescaledb;
DROP SCHEMA unpinned_target;

-- a precreated superuser-owned schema is usable.
RESET ROLE;
CREATE SCHEMA pinned_stub;
SET ROLE admin;
CREATE EXTENSION pinned_stub VERSION '0.0.0';

-- ALTER EXTENSION UPDATE is blocked when the schema is not superuser-owned.
RESET ROLE;
ALTER SCHEMA pinned_stub OWNER TO mere_mortal;
SET ROLE admin;
ALTER EXTENSION pinned_stub UPDATE TO '0.0.1';

-- with the schema back under superuser ownership, the update succeeds.
RESET ROLE;
ALTER SCHEMA pinned_stub OWNER TO CURRENT_USER;
SET ROLE admin;
ALTER EXTENSION pinned_stub UPDATE TO '0.0.1';
DROP EXTENSION pinned_stub;

RESET ROLE;
DROP SCHEMA pinned_stub;

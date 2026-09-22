SET ROLE admin;

CREATE SCHEMA ine;

CREATE EXTENSION citext SCHEMA ine;

-- a catalog shadow in the extension schema would normally block CREATE
CREATE TABLE ine.pg_type(id int);

-- IF NOT EXISTS on an installed extension is a no-op, so it is not blocked
CREATE EXTENSION IF NOT EXISTS citext;
CREATE EXTENSION IF NOT EXISTS citext SCHEMA ine;
CREATE EXTENSION IF NOT EXISTS citext SCHEMA public;

-- without IF NOT EXISTS the shadow check still applies
CREATE EXTENSION citext SCHEMA ine;

-- IF NOT EXISTS on an extension that is not installed is still checked
CREATE EXTENSION IF NOT EXISTS pg_trgm SCHEMA ine;

DROP TABLE ine.pg_type;

-- and succeeds once the shadow is gone
CREATE EXTENSION IF NOT EXISTS pg_trgm SCHEMA ine;

DROP EXTENSION pg_trgm;
DROP EXTENSION citext;
DROP SCHEMA ine;

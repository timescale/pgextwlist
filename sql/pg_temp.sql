
SET ROLE admin;

-- verify baseline: whitelisted extension can be created normally
CREATE EXTENSION citext;
SELECT extname FROM pg_extension WHERE extname = 'citext';
DROP EXTENSION citext;

-- create a temporary table to populate pg_temp
CREATE TEMP TABLE pg_proc AS SELECT * FROM pg_catalog.pg_proc;

-- whitelisted extension should be blocked when pg_temp is not empty
CREATE EXTENSION citext;
SELECT extname FROM pg_extension WHERE extname = 'citext';

-- clean up temp table
DROP TABLE pg_temp.pg_proc;

-- now with pg_temp empty, creation should succeed again
CREATE EXTENSION citext;
SELECT extname FROM pg_extension WHERE extname = 'citext';

-- clean up
DROP EXTENSION citext;

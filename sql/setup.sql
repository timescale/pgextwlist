LOAD 'pgextwlist';
ALTER SYSTEM SET session_preload_libraries='pgextwlist';
ALTER SYSTEM SET extwlist.extensions='citext,earthdistance,pg_trgm,pg_stat_statements,refint';
\set testdir `pwd` '/test-scripts'
ALTER SYSTEM SET extwlist.custom_path=:'testdir';
SELECT pg_reload_conf();

CREATE ROLE mere_mortal;

CREATE OR REPLACE FUNCTION set_extwlist_extname_from_filename() RETURNS VOID SECURITY DEFINER AS $$
    SET extwlist.extname_from_filename TO true;
$$ LANGUAGE SQL;

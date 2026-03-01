\set testdir `pwd` '/test-scripts'
ALTER SYSTEM SET extwlist.custom_path=:'testdir';
SELECT pg_reload_conf();

CREATE ROLE admin;
CREATE ROLE mere_mortal;

GRANT CREATE ON DATABASE contrib_regression TO admin;

CREATE OR REPLACE FUNCTION set_extwlist_extname_from_filename() RETURNS VOID SECURITY DEFINER AS $$
    SET extwlist.extname_from_filename TO true;
$$ LANGUAGE SQL;

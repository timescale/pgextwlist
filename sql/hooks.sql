select case
  when setting::int >= 160000 then 'PG 16+'
  end as "regression output for PG version"
from pg_settings where name = 'server_version_num';

set client_min_messages = debug;
set extwlist.custom_path = '';
set role mere_mortal;

-- in the hope autoinc stays at version 1.0 in PostgreSQL
create extension autoinc;
alter extension autoinc update;
comment on extension autoinc is 'snarky remark';
drop extension autoinc, autoinc;

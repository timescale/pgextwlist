short_ver = $(shell git describe --match "v[0-9]*" --abbrev=4 HEAD | cut -d- -f1 | sed 's/^v//')
long_ver = $(shell (git describe --tags --long '--match=v*' 2>/dev/null || echo $(short_ver)-0-unknown) | cut -c2-)

MODULE_big = pgextwlist
OBJS       = utils.o pgextwlist.o
DOCS       = README.md
REGRESS    = setup pgextwlist errors crossuser hooks pg_temp catalog_shadow variadic_shadow alter_update_shadow
REGRESS_OPTS += --temp-instance=./tmp_check --temp-config=test.conf
RPM_MINOR_VERSION_SUFFIX ?=

PG_CONFIG = pg_config
PGXS = $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

# Stub "timescaledb" extension used by the variadic_shadow regression test
# to exercise the timescaledb-only branch of the function-shadow check
# without depending on a real TimescaleDB install. Writes to the host
# sharedir, so this target must be run with the same privileges as
# `make install`.
#
# If a real TimescaleDB is installed, we keep its control file and only
# add our --0.0.0.sql alongside its many version files. The regression
# test uses CREATE EXTENSION timescaledb VERSION '0.0.0' so the real
# .so is never loaded and the preload check is skipped.
EXT_DIR := $(shell $(PG_CONFIG) --sharedir)/extension

.PHONY: install-tsdb-stub uninstall-tsdb-stub
install-tsdb-stub:
	@if [ -e "$(EXT_DIR)/timescaledb.control" ] && \
	    ! grep -q "pgextwlist test stub" "$(EXT_DIR)/timescaledb.control"; then \
	  echo "real timescaledb at $(EXT_DIR), keeping its control file"; \
	else \
	  $(INSTALL_DATA) test-stub/timescaledb.control $(EXT_DIR)/; \
	fi
	$(INSTALL_DATA) test-stub/timescaledb--0.0.0.sql $(EXT_DIR)/
	$(INSTALL_DATA) test-stub/timescaledb--0.0.1.sql $(EXT_DIR)/
	$(INSTALL_DATA) test-stub/timescaledb--0.0.0--0.0.1.sql $(EXT_DIR)/

uninstall-tsdb-stub:
	@if [ -e "$(EXT_DIR)/timescaledb.control" ] && \
	    grep -q "pgextwlist test stub" "$(EXT_DIR)/timescaledb.control"; then \
	  rm -f $(EXT_DIR)/timescaledb.control; \
	fi
	rm -f $(EXT_DIR)/timescaledb--0.0.0.sql
	rm -f $(EXT_DIR)/timescaledb--0.0.1.sql
	rm -f $(EXT_DIR)/timescaledb--0.0.0--0.0.1.sql

DEBUILD_ROOT = /tmp/pgextwlist

deb:
	mkdir -p $(DEBUILD_ROOT) && rm -rf $(DEBUILD_ROOT)/*
	rsync -Ca --exclude=build/* ./ $(DEBUILD_ROOT)/
	cd $(DEBUILD_ROOT) && make -f debian/rules orig
	cd $(DEBUILD_ROOT) && debuild -us -uc -sa
	cp -a /tmp/pgextwlist_* /tmp/postgresql-9.* build/

rpm:
	git archive --output=pgextwlist-rpm-src.tar.gz --prefix=pgextwlist/ HEAD
	rpmbuild -bb pgextwlist.spec \
		--define '_sourcedir $(CURDIR)' \
		--define 'package_prefix $(package_prefix)' \
		--define 'pkglibdir $(shell $(PG_CONFIG) --pkglibdir)' \
		--define 'major_version $(short_ver)' \
		--define 'minor_version $(subst -,.,$(subst $(short_ver)-,,$(long_ver)))$(RPM_MINOR_VERSION_SUFFIX)'
	$(RM) pgextwlist-rpm-src.tar.gz

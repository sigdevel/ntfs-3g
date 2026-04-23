# -- Configuration ------------------------------------------------------------
CLANG ?= clang

TESTS   := tests
LIB     := libntfs-3g
BUILD_T := build_tests

# -- Compiler flags -----------------------------------------------------------
CFLAGS := \
	-std=c11 -O0 -g \
	-Wall -Wextra -Werror \
	-Wno-address-of-packed-member \
	-Wno-unused-but-set-variable \
	-fsanitize=address,undefined \
	-fno-omit-frame-pointer \
	-ffunction-sections -fdata-sections \
	-DHAVE_CONFIG_H -D_FILE_OFFSET_BITS=64 \
	-I$(TESTS) -I include/ntfs-3g -I include

LDFLAGS := -fsanitize=address,undefined -Wl,--gc-sections

# -- Common library objects ---------------------------------------------------
LIB_COMMON := $(LIB)/logging.c $(LIB)/misc.c

# -- Test binaries ------------------------------------------------------------
TESTS_ALL := \
	$(BUILD_T)/test_misc_logging \
	$(BUILD_T)/test_cache \
	$(BUILD_T)/test_mst \
	$(BUILD_T)/test_bootsect \
	$(BUILD_T)/test_runlist \
	$(BUILD_T)/test_acls_security \
	$(BUILD_T)/test_unistr \
	$(BUILD_T)/test_attrib_index_mft \
	$(BUILD_T)/test_logfile

# -- Corpus stamp -------------------------------------------------------------
CORPUS_STAMP := $(TESTS)/corpus/.stamp

# -- Targets ------------------------------------------------------------------
.PHONY: all test test-all clean

all: test

# -- Corpus generation --------------------------------------------------------
$(CORPUS_STAMP): scripts/generate_corpus.py | $(TESTS)/corpus
	python3 scripts/generate_corpus.py $(TESTS)/corpus
	touch $@

$(TESTS)/corpus:
	mkdir -p $@

# -- Unit tests (ASan + UBSan) ------------------------------------------------
test: $(TESTS_ALL) $(CORPUS_STAMP)
	$(BUILD_T)/test_misc_logging
	$(BUILD_T)/test_cache
	$(BUILD_T)/test_mst
	$(BUILD_T)/test_bootsect
	$(BUILD_T)/test_runlist
	$(BUILD_T)/test_acls_security
	$(BUILD_T)/test_unistr
	$(BUILD_T)/test_attrib_index_mft
	$(BUILD_T)/test_logfile

test-all: test

$(BUILD_T)/test_misc_logging: $(TESTS)/test_misc_logging.c $(LIB_COMMON) | $(BUILD_T)
	$(CLANG) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_T)/test_cache: $(TESTS)/test_cache.c $(LIB)/cache.c $(LIB_COMMON) | $(BUILD_T)
	$(CLANG) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_T)/test_mst: $(TESTS)/test_mst.c $(LIB)/mst.c $(LIB_COMMON) | $(BUILD_T)
	$(CLANG) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_T)/test_bootsect: $(TESTS)/test_bootsect.c $(LIB)/bootsect.c $(LIB_COMMON) | $(BUILD_T)
	$(CLANG) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_T)/test_runlist: $(TESTS)/test_runlist.c $(LIB)/runlist.c $(LIB_COMMON) | $(BUILD_T)
	$(CLANG) $(CFLAGS) -Wno-format -DNTFS_TEST -o $@ $^ $(LDFLAGS)

$(BUILD_T)/test_acls_security: \
		$(TESTS)/test_acls_security.c \
		$(LIB)/acls.c $(LIB)/security.c $(LIB_COMMON) | $(BUILD_T)
	$(CLANG) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_T)/test_unistr: $(TESTS)/test_unistr.c $(LIB)/unistr.c $(LIB_COMMON) | $(BUILD_T)
	$(CLANG) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_T)/test_attrib_index_mft: \
		$(TESTS)/test_attrib_index_mft.c \
		$(LIB)/attrib.c $(LIB)/index.c $(LIB)/mft.c $(LIB_COMMON) | $(BUILD_T)
	$(CLANG) $(CFLAGS) -Wno-format -o $@ $^ $(LDFLAGS)

$(BUILD_T)/test_logfile: \
		$(TESTS)/test_logfile.c \
		$(LIB)/logfile.c $(LIB)/mst.c $(LIB_COMMON) | $(BUILD_T)
	$(CLANG) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_T):
	mkdir -p $@

# -- Clean build artifacts ----------------------------------------------------
clean:
	rm -rf $(BUILD_T)
	rm -f $(CORPUS_STAMP)

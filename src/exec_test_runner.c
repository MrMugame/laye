#include <assert.h>
#include <stdio.h>

#define NOB_NO_LOG_EXIT_STATUS
#define NOB_NO_CMD_RENDER

#define NOB_IMPLEMENTATION
#include "nob.h"

#include "ansi.h"

#define LCA_DA_IMPLEMENTATION
#include "lcads.h"

#define LAYEC_PATH "./out/layec"

#define FCHK_DIR  "./fchk"
#define FCHK_OUT  FCHK_DIR "/out"
#define FCHK_PATH FCHK_OUT "/fchk"

typedef struct test_info {
    const char* test_name;
} test_info;

typedef struct test_state {
    int test_count;
    dynarr(test_info) failed_tests;
} test_state;

static bool cstring_ends_with(const char* s, const char* ending);
static bool run_exec_test(const char* test_file_path);
static void run_tests_in_directory(test_state* state, const char* test_directory, const char* extension);

int main(int argc, char** argv) {
    const char* program = nob_shift_args(&argc, &argv);

    test_state state = {0};
    run_tests_in_directory(&state, "./test/laye", ".laye");

    int64_t num_tests_failed = arr_count(state.failed_tests);
    if (num_tests_failed == 0) {
        fprintf(
            stderr,
            "\n%s100%% of tests passed%s out of %d.\n",
            ANSI_COLOR_GREEN,
            ANSI_COLOR_RESET,
            state.test_count
        );
    } else {
        int percent_success = (state.test_count - num_tests_failed) * 100 / state.test_count;
        fprintf(
            stderr,
            "\n%d%% of tests passed, %s%d tests failed%s out of %d.\n",
            percent_success,
            ANSI_COLOR_RED,
            (int)num_tests_failed,
            ANSI_COLOR_RESET,
            state.test_count
        );

        fprintf(stderr, "\nThe following tests FAILED:\n");
        for (int64_t i = 0; i < num_tests_failed; i++) {
            fprintf(stderr, "\t%s%s%s\n", ANSI_COLOR_RED, state.failed_tests[i].test_name, ANSI_COLOR_RESET);
        }
    }

    arr_free(state.failed_tests);
    nob_temp_reset();

    return 0;
}

static void run_tests_in_directory(test_state* state, const char* test_directory, const char* extension) {
    Nob_File_Paths test_file_paths = {0};
    if (!nob_read_entire_dir(test_directory, &test_file_paths)) {
        nob_log(NOB_ERROR, "Failed to enumerate test directory");
        return;
    }

    const char* noexec_extension = nob_temp_sprintf(".noexec%s", extension);

    for (size_t i = 0; i < test_file_paths.count; i++) {
        const char* test_file_path = test_file_paths.items[i];
        if (!cstring_ends_with(test_file_path, extension)) {
            continue;
        }

        const char* test_full_name = nob_temp_sprintf("%s/%s", test_directory, test_file_path);

        bool is_noexec = cstring_ends_with(test_file_path, noexec_extension);
        if (is_noexec) continue;

        bool exec_test_success = run_exec_test(test_full_name);
        state->test_count++;
        if (!exec_test_success) {
            arr_push(state->failed_tests, ((test_info){ .test_name = test_full_name }));
        }
    }

    nob_da_free(test_file_paths);
}

static bool cstring_ends_with(const char* s, const char* ending) {
    size_t sLength = strlen(s);
    size_t endingLength = strlen(ending);

    if (sLength < endingLength) {
        return false;
    }

    return 0 == strncmp(s + sLength - endingLength, ending, endingLength);
}

#define INVALID_EXIT_CODE (-255)

static int read_expected_exit_code(const char* test_file_path) {
    FILE* s = fopen(test_file_path, "r");
    if (s == NULL) {
        return INVALID_EXIT_CODE;
    }

    int expected_exit_code = 0;
    if (fscanf(s, "// %d", &expected_exit_code) != 1) {
        fclose(s);
        return INVALID_EXIT_CODE;
    }

    fclose(s);
    return expected_exit_code;
}

static bool run_exec_test(const char* test_file_path) {
    nob_log(NOB_INFO, "-- Running execution test for \"%s\"", test_file_path);

#ifdef _WIN32
    const char* exec_file = nob_temp_sprintf("%s.out.exe", test_file_path);
#else
    const char* exec_file = nob_temp_sprintf("%s.out", test_file_path);
#endif

    Nob_Cmd cmd = {0};
    nob_cmd_append(
        &cmd,
        LAYEC_PATH,
        "-o",
        exec_file,
        test_file_path
    );

    bool compile_success = nob_cmd_run_sync(cmd);
    if (!compile_success) {
        nob_cmd_free(cmd);
        return false;
    }

    cmd.count = 0;
    nob_cmd_append(&cmd, exec_file);

    Nob_Proc_Result exec_result = nob_cmd_run_sync_result(cmd);
    nob_cmd_free(cmd);
    remove(exec_file);

    int expected_exit_code = read_expected_exit_code(test_file_path);
    if (expected_exit_code == INVALID_EXIT_CODE) {
        return false;
    }

    return expected_exit_code == exec_result.exit_code;
}

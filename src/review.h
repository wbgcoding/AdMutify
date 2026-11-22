#pragma once

// Surface review tool, built only with ADMUTIFY_REVIEW.
// Handles --review ids, --shot (with --lang, --theme, --out, --hold), --dpi-test, --fit-test, --combo-keys-test,
// --music-radio-test, --crash-test and --crash-test-nothread. Returns true when the command line was a review command.
bool RunReviewCommand(int argc, wchar_t** argv, int& exitCode);

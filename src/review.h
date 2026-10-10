#pragma once

// Surface review tool, built only with ADMUTIFY_REVIEW.
// Handles --review ids, --shot and --crash-test. Returns true when the command line was a review command.
bool RunReviewCommand(int argc, wchar_t** argv, int& exitCode);

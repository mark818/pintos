# -*- perl -*-
use strict;
use warnings;
use tests::tests;
use tests::random;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(student-test-1) begin
(student-test-1) create "testfile"
(student-test-1) open "testfile"
(student-test-1) writing "testfile"
(student-test-1) open "testfile" for verification
(student-test-1) disk writes upper range correct
(student-test-1) disk writes lower range correct
(student-test-1) end
EOF
pass;
# -*- perl -*-
use strict;
use warnings;
use tests::tests;
use tests::random;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(student-test-2) begin
(student-test-2) create "testfile"
(student-test-2) open "testfile"
(student-test-2) writing "testfile"
(student-test-2) close "testfile"
(student-test-2) reset cache
(student-test-2) open "testfile"
(student-test-2) reading "testfile"
(student-test-2) close "testfile"
(student-test-2) open "testfile"
(student-test-2) reading "testfile" 2nd time
(student-test-2) close "testfile"
(student-test-2) first time read caused 51 disk reads
(student-test-2) second time read caused 0 disk reads
(student-test-2) cache hit rate improved in the second read
(student-test-2) end
EOF
pass;
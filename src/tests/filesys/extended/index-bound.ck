use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(index-bound) begin
(index-bound) INDEX 8MiB boundaries and full reclamation PASS
(index-bound) end
index-bound: exit(0)
EOF
pass;

# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(sync-bound) begin
(sync-bound) PASS
(sync-bound) end
sync-bound: exit(0)
EOF
pass;

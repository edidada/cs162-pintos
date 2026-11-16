# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(alarm-block) begin
(alarm-block) PASS
(alarm-block) end
EOF
pass;

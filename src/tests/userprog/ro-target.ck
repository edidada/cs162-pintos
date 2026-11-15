# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
ro-target: before read into code
ro-target: exit(-1)
EOF
pass;

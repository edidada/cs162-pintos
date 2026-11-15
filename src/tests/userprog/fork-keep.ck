# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
fork-keep: exit(7)
fork-keep: private memory and shared offset survive child close
fork-keep: exit(0)
EOF
pass;

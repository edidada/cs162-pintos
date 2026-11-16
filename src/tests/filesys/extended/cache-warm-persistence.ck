use strict;
use warnings;
use tests::tests;
check_archive ({"warm-file" => [join("", map { chr($_ % 251) } 0..8191)]});
pass;

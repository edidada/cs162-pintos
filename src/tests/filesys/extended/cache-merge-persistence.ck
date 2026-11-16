use strict;
use warnings;
use tests::tests;
check_archive ({"merge-file" => [join("", map { chr($_ % 251) } 0..65535)]});
pass;

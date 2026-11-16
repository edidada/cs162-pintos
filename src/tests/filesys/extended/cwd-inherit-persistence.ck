use strict;
use warnings;
use tests::tests;
check_archive ({
  "a" => {"marker" => ["data"], "child-a" => [""]},
  "b" => {"forked" => [""], "child-b" => [""]},
  "cwd-child" => "tests/filesys/extended/cwd-child"
});
pass;

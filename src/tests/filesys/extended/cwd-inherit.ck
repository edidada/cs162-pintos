use strict;
use warnings;
use tests::tests;
our $test;
my @output = read_text_file("$test.output");
common_checks("run", @output);
my $text = join("\n", @output);
fail "Missing cwd verification" unless $text =~ /CWD fork\/exec inheritance and independent changes PASS/ && $text =~ /cwd-inherit: exit\(0\)/;
pass;

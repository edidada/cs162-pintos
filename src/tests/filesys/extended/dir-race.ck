use strict;
use warnings;
use tests::tests;
our $test;
my @output = read_text_file("$test.output");
common_checks("run", @output);
my $text = join("\n", @output);
fail "Missing race verification" unless $text =~ /RACE unique names and space recovery PASS/ && $text =~ /dir-race: exit\(0\)/;
pass;

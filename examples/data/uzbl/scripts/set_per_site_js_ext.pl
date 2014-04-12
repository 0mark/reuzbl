#!/usr/bin/perl

use strict;
use File::Spec;

my $file = ($ENV{XDG_DATA_HOME} ? $ENV{XDG_DATA_HOME} : $ENV{HOME}.'/.local/share').'/uzbl/per-site';
my $mode = $ARGV[7];

my @setargs = splice(@ARGV, 8);
if ($#setargs == 0) {
  @setargs = split(/ /, $setargs[0]);
}

my $url = $ARGV[5];
if ($mode eq "domain") {
  $url =~ s/(.*?\/\/[^\/]*?\/).*/\1/g;
} elsif ($mode eq "host") {
  $url =~ s/([^:]+:\/\/)([^.]+\.)*([^.]+\.[^\/]+\/).*/\1*\3/g;
} else {
  $url =~ s/(\/)[^\/]*$/\1/g;
}

my %settings;# = ('p' => 0, 's'=>0, 'x' => 0, 'i' => 1, 'r' => 0);

open (FILE, "<$file") or die $!;
my @file = <FILE>;
close (FILE);

my $done=0;
for (my $i=0; $i<=$#file; $i++) {
  my @l = split(/ /, $file[$i]);
  # replace existing setting
  if ($url."\n" eq $l[$#l]) {
    if ($setargs[0] eq 'remove') {
      splice(@file, $i, 1);
      $done=1;
      last;
    } else {
      # read the old settings
      foreach (splice(@l, 0, -1)) {
	$settings{substr($_, 0, 1)} = substr($_, 1);
      }
      # overwright with new settings
      foreach (@setargs) {
	$settings{substr($_, 0, 1)} = substr($_, 1);
      }
      # convert settings to string
      $file[$i] = $l[$#l];
      while ((my $key, my $val) = each(%settings)) {
	$file[$i] = $key.$val.' '.$file[$i];
      }
      $done=1;
      last;
    }
  }
}

# new setting
if ($done == 0) {
  foreach (@setargs) {
    $settings{substr($_, 0, 1)} = substr($_, 1);
  }
  $file[$#file+1] = $url."\n";
  while ((my $key, my $val) = each(%settings)) {
    $file[$#file] = $key.$val.' '.$file[$#file];
  }
}

open (FILE, ">$file") or die $!;
foreach (@file) {
  print FILE $_;
}
close (FILE);

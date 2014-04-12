#!/usr/bin/perl

# Finding the last Element in history whose basename equals the current url

use strict;
use File::Basename;

my $url = shift;

my @hstry = reverse(split(/\n/, `cat \$XDG_DATA_HOME/uzbl/history | grep $url | cut -d " " -f 3`));

foreach (@hstry) {
  if (dirname($_).'/' eq $url) {
    print "set uri = ".$_."\n";
    exit;
  }
}

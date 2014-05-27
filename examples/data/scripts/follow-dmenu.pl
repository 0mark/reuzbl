#!/usr/bin/env perl

# OH DUDE WHAT IZ ME BETA!!!

# This is a unique Link following Script for reuzbl. Instead of just numberize
# visible Links in the Viewspace, dmenu ist utilized to select from all Links
# by the Title of the Link. This is way more convinient for often used sites
# where the user knows all links by surename.
#
# There are a lot of Problems. Multiple Links of the same Title arent handled
# properly (only the first is selectable). Instead of the "title" (whatever that
# may be) just the innerHTML is used. There will be Bugs, too.
#
# For bugreports, fixes, feature requests or correction of my writing call me
# be jabber or Email: 0mark at unserver.de
#
# getLinks.js is salvaged from follow_Numbers.js
# the Socket Object from the reuzbl-ctrl.pl script.
#
# TODO
# soon:
# - find and use the biggest textnode child of an anchor as title (JS)
# - if none found, use alt, title, ... attribute from images, ... (JS)
# - remove or change all non-ascii char (Perl) (or patch utf8 into dmenu)
# - numberize multiple titles (Perl)
# !soon:
# - more fastness, maybe use less packages or recode in lua or c or such alike
# - find everything clickable (does this makes sense?)

use strict;

use IO::Socket::UNIX;
use Digest::MD5 qw(md5_hex);
use IPC::Open2;
use FindBin '$Bin';

# There should be a default source for this for all Scripts!
my $dmenuopts = " -nb #303030 -nf khaki -sb #CCFFAA -sf #303030";

sub Socket {
  my $path = shift;
  print ">$path\n";
  # connect the socket
  my $socket = IO::Socket::UNIX->new(Type => SOCK_STREAM, Peer => $path) or die $@;
  return sub {
    my $line = shift;
    # generate a end marker
    my $mark = md5_hex(rand());
    # send the command and request endmarker
    print $socket "$line\nprint $mark\n";
    my $str;
    while(my $line = <$socket>){
      #read until end marker
      if($line =~ m/^$mark$/){
        chomp($str);
        return $str;
      } else {
        $str .= $line;
      }
    }
  }
}

my $query = Socket($ARGV[4]);
my $new = $ARGV[7];

# Let reuzbl run a javascript that returns a list of links
my @result = split(/\n/, $query->('script '.$Bin.'/getLinks.js'));

my $titles;
my %urls;

foreach(@result) {
  my @e = split(/\+\+\+/, $_);
  $titles .= $e[1]."\n";
  $urls{$e[1]} = $e[0];
}

# Use dmenu to select one of the Links
my $RDR;
my $WRTR;
my $pid = open2($RDR, $WRTR, "dmenu -i -l 20 \$DMENUSETTINGS") || die "Mäh";
print $WRTR $titles;
close($WRTR);
my @val = <$RDR>;
close($RDR);
$val[0] =~ s/\n//g;
exit if($val[0] eq "");
my $url = $urls{$val[0]};

# Tell reuzbl to use the selected url
if($new eq "new") {
  $query->("chain 'event SET_KEYCMD \@OpenInNewWindowKey' 'event APPEND_KEYCMD $url' 'event KEYCMD_EXEC_CURRENT' 'event SET_KEYCMD'");
} else {
  $query->("uri $url");
}

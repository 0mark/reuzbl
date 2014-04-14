#!/usr/bin/perl

use strict;
use File::Spec;
use Digest::MD5 qw(md5_hex);
use IO::Socket::UNIX;

sub Socket {
  my $path = shift;
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
my $file = ($ENV{XDG_DATA_HOME} ? $ENV{XDG_DATA_HOME} : $ENV{HOME}.'/.local/share').'/uzbl/per-site';
my $base = my $host = $ARGV[5];
$base =~ s/\/[^\/]*$//g;
$host =~ s/([^:]+:\/\/)([^.]+\.)*([^.]+\.[^\/]+\/).*/\1*\3/g;
my @dirs = File::Spec->splitdir($base);
my @urls = ();

for(my $i=0; $i<=$#dirs; $i++) {
  $urls[$i+1] = $urls[$i].$dirs[$i].'/';
}
@urls=splice(@urls,3);
push(@urls, ($host));

open (FILE, "<$file") or die $!;
my @file = <FILE>;
close (FILE);

my %settings = ('p' => 0, 's'=>0, 'x' => 0, 'i' => 1, 'r' => 0);

foreach my $url (@urls) {
  my @lines = grep {index($_, $url)>0} @file;
  if ($#lines>=0) {
    foreach(split(/ /, @lines[0])) {
      $settings{substr($_,0,1)} = substr($_,1);
    }
  }
}

my $p = 'set disable_plugins='.abs(1-$settings{'p'});
my $s = 'set disable_scripts='.abs(1-$settings{'s'});
my $i = 'set autoload_images='.$settings{'i'};
my $x = 'set proxy_url='.(($settings{'x'}==0)?'':$settings{'x'});
my $r = 'set enable_private='.$settings{'r'};

$query->("chain '$p' '$s' '$i'  '$r'");
#system('notify-send', 'settings', "chain '$p' '$s' '$i'  '$r'");

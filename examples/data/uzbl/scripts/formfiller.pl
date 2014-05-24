#!/usr/bin/perl

# a slightly more advanced form filler
#
# uses settings file like: $keydir/<domain>
#TODO: fallback to $HOME/.local/share
# user arg 1:
# edit: force editing of the file (fetches if file is missing)
# load: fill forms from file (fetches if file is missing)
# new:  fetch new file

# usage example:
# bind LL = spawn /usr/share/reuzbl/examples/data/scripts/formfiller.pl load mode
# bind LN = spawn /usr/share/reuzbl/examples/data/scripts/formfiller.pl new mode
# bind LE = spawn /usr/share/reuzbl/examples/data/scripts/formfiller.pl edit mode
# mode is one of d (Domain), p (Path), g (Get), a (all), and defines the part of
# the url which is used to identify the form

use strict;
#use warnings;


use IO::Socket::UNIX;
use Digest::MD5 qw(md5_hex);
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
my $keydir = $ENV{XDG_DATA_HOME} . "/reuzbl/forms";
my ($config,$pid,$xid,$fifoname,$socket,$url,$title,$cmd,$mode) = @ARGV;
if (!defined $fifoname || $fifoname eq "") { die "No fifo"; }

sub domain {
  my ($url) = @_;
  $url =~ s#http(s)?://([A-Za-z0-9\.-]+)(/.*)?#$2#;
  return $url;
};
sub path {
  my ($path) = @_;
  print $mode;
  if($mode eq 'd') {
    $path =~ s#http(s)?://([A-Za-z0-9\.-]+)(/.*)?#$2#;
  } elsif ($mode eq 'p') {
    $path =~ s#http(s)?://([A-Za-z0-9\.-]+)(/(.*)(\?.*)?)?#$2/$4#;
  } elsif ($mode eq 'g') {
    $path =~ s#http(s)?://([A-Za-z0-9\.-]+)(/(.*)(\?.*)?)?#$2/$5#;
  } elsif ($mode eq 'a') {
    $path = $path;
  }
  return $path;
};

my $editor = "urxvt -e nano ";

my @fields = ("type","name","value");

my %command;

$command{load} = sub {
  my ($url) = @_;
  my $domain=domain($url);
  my $filename = "$keydir/$domain/".md5_hex(path($url));
  if (-e $filename){
    open(my $file, $filename) or die "Failed to open $filename: $!";
    my (@lines) = <$file>;
    close($file);
    $|++;
    open(my $fifo, ">>", $fifoname) or die "Failed to open $fifoname: $!";
    foreach my $line (@lines) {
      next if ($line =~ m/^#/);
      my ($type,$name,$value) = ($line =~ /^\s*(\w+)\s*\|\s*(.*?)\s*\|\s*(.*?)\s*$/);
      if ($type eq "checkbox")
      {
        printf $fifo 'js document.getElementsByName("%s")[0].checked = %s;', $name, $value;
      } elsif ($type eq "submit")
      {
        printf $fifo 'js function fs (n) {try{n.submit()} catch (e){fs(n.parentNode)}}; fs(document.getElementsByName("%s")[0]);', $name;
      } elsif ($type ne "")
      {
        printf $fifo 'js document.getElementsByName("%s")[0].value = "%s";', $name, $value;
      }
      print $fifo "\n";
    }
    $|--;
  } else {
    $command{new}->($url);
    $command{edit}->($url);
  }
};
$command{edit} = sub {
  my ($url) = @_;
  my $domain=domain($url);
  my $filename = "$keydir/$domain/".md5_hex(path($url));
  print $filename."\n";
  if(-e $filename){
    system ($editor.' '.$filename);
  } else {
    $command{new}->($url);
  }
};
$command{new} = sub {
  my ($url) = @_;
  my $domain=domain($url);
  if(!-d "$keydir/$domain/") {
    mkdir("$keydir/$domain/");
  }
  my $filename = "$keydir/$domain/".md5_hex(path($url));
  open (my $file,">", $filename) or die "Failed to open $filename: $!";
  $|++;
  print $file "# Make sure that there are no extra submits, since it may trigger the wrong one.\n";
  printf $file "#%-10s | %-10s | %s\n", @fields;
  print $file "#------------------------------\n";
  my @data = split(/>/, $query->("js document.documentElement.outerHTML"));
  foreach my $line (@data){
    $line.='>';
    if($line =~ m/<input ([^>].*?)>/i){
      $line =~ s/.*(<input ([^>].*?)>).*/$1/;
      printf $file " %-10s | %-10s | %s\n", map { my ($r) = $line =~ /.*$_=["'](.*?)["']/;$r } @fields;
    };
  };
  $|--;
};


$command{$cmd}->($url);

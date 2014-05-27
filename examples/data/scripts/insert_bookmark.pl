#!/usr/bin/perl -w

use IPC::Open2;
use strict;

my $file = ($ENV{XDG_DATA_HOME} ? $ENV{XDG_DATA_HOME} : $ENV{HOME}.'/.local/share').'/reuzbl/bookmarks';
my $purl = $ARGV[5];
my $ptitle = $ARGV[6];
my $guithingy = $ARGV[7] ? $ARGV[7] : '';

my $RDR; my $WRTR;
if($guithingy eq 'zenity') {
    my $pid=open2($RDR, $WRTR, "zenity --entry --text=\"Add bookmark, add tags after the '-', separated by spaces\" --entry-text=\"$ptitle : \"") || die "Mäh";
} else {
    # Use dmenu to select one of the Links. TODO: dmenu patch for "edit mode": same as pressing ctrl-i from start.
    my $pid=open2($RDR, $WRTR, "dmenu \$DMENUSETTINGS") || die "Mäh";
}
print $WRTR $ptitle.' : ';
close($WRTR);
my @val = <$RDR>;
close($RDR);
exit if ($val[0] eq "");
my $ntags = $val[0];
my $ntitle = '';
$ntags =~ s/^(.*) : ([^:]*)$/$2/g;
$ntitle = $1;

# Use Zenity to edit the Link


open (FILE, "<$file") or die $!; my @file = <FILE>; close (FILE);
# remove trailing linebreaks
splice(@file,-1) if ($file[$#file] eq "\n");

my $found=0;
@file=map {
	my $s=$_;
	$s=~s/^(.*?) .*/$1/g;
	$s=substr($s,0,-1);
	if($s eq $purl) {
		$found=1;
		$purl.' '.$ntitle."\t".$ntags."\n";
	} else { $_; }
} @file;

$file[$#file+1]=$purl.' '.$ntitle."\t".$ntags."\n" if ($found==0);

open (FILE, ">$file") or die $!;
foreach (@file) { print FILE $_; }
close (FILE);

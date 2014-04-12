#!/usr/bin/perl -w

use strict;
use Gtk2 '-init';
use File::Spec;

my $file = ($ENV{XDG_DATA_HOME} ? $ENV{XDG_DATA_HOME} : $ENV{HOME}.'/.local/share').'/uzbl/per-site';
my $base = my $host = $ARGV[5];
$base =~ s/\/[^\/]*$//g;
$host =~ s/([^:]+:\/\/)([^.]+\.)*([^.]+\.[^\/]+\/).*/$1*$3/g;
my @dirs = File::Spec->splitdir($base);
my @urls = ('');

for(my $i=0; $i<=$#dirs; $i++) {
  $urls[$i+1] = $urls[$i].$dirs[$i].'/';
}
@urls = splice(@urls,3);
unshift(@urls, ($host));
# TODO create file if not exists!
open (FILE, "<$file") or die $!;
my @file = <FILE>;
close (FILE);

my %settings = ('p' => 0, 's'=>0, 'x' => 0, 'i' => 1, 'r' => 0);

my $found=0;
foreach my $url (@urls) {
  my @lines = grep {index($_, $url)>0} @file;
  if ($#lines>=0) {
    $found=$url;
    foreach(split(/ /, $lines[0])) {
      $settings{substr($_,0,1)} = substr($_,1);
    }
  }
}

my $mode='';
my $typeBox = Gtk2::HBox->new();
my $bydir;
my $bydomain;
my $byhost;
if ($found) {
  if($found eq $urls[$#urls]) { $mode=' dir'; }
  if($found eq $urls[1]) { $mode=' domain'; }
  if($found eq $urls[0]) { $mode=' host'; }
  $typeBox->pack_start(Gtk2::Label->new($found), 0, 1, 0);
} else {
  $bydir = Gtk2::RadioButton->new(undef, $urls[$#urls]);
  my @radiogroup = $bydir->get_group;
  $typeBox->pack_start($bydir, 0, 1, 0);

  $bydomain = Gtk2::RadioButton->new(@radiogroup, $urls[1]);
  $bydomain->set_active(1);
  $typeBox->pack_start($bydomain, 0, 1, 0);

  $byhost = Gtk2::RadioButton->new(@radiogroup, $urls[0]);
  $typeBox->pack_start($byhost, 0, 1, 0);
}

my $scripts = Gtk2::CheckButton->new("Allow JavaScript");
$scripts->set_active(abs(1-$settings{'s'})==0);

my $plugins = Gtk2::CheckButton->new("Allow Plugins");
$plugins->set_active(abs(1-$settings{'p'})==0);

my $images = Gtk2::CheckButton->new("Autoload Images");
$images->set_active($settings{'i'}==1);

#my $private = Gtk2::CheckButton("Enable Private");
#$private.set_active($settings{'i'});

my $proxyBox = Gtk2::HBox->new();
$proxyBox->pack_start(Gtk2::Label->new('Proxy'), 0, 1, 0);
my $proxy = Gtk2::Entry->new();
$proxy->set_text( ($settings{'x'} eq "0")?'': $settings{'x'});
$proxy->signal_connect (activate => \&button_callback);
$proxyBox->pack_start($proxy, 1, 1, 0);

my $settingsBox = Gtk2::VBox->new();
$settingsBox->pack_start($scripts, 0, 1, 0);
$settingsBox->pack_start($plugins, 0, 1, 0);
$settingsBox->pack_start($images, 0, 1, 0);
#$settingsBox->pack_start($private, 0, 1, 0);
$settingsBox->pack_start($proxyBox, 0, 1, 0);

my $buttonBox = Gtk2::HBox->new();
my $ok = Gtk2::Button->new (' Ok ');
my $cancel = Gtk2::Button->new (' Cancel ');
my $remove = Gtk2::Button->new (' Remove ');
$buttonBox->pack_start($ok, 0, 1, 0);
$buttonBox->pack_start($cancel, 0, 1, 0);
if($found) { $buttonBox->pack_end($remove, 0, 1, 0); }
$ok->signal_connect (clicked => \&button_ok);
$cancel->signal_connect (clicked => sub { Gtk2->main_quit; });
$remove->signal_connect (clicked => \&button_remove);

my $vbox = Gtk2::VBox->new();
$vbox->pack_start($typeBox, 1, 1, 0);
$vbox->pack_start($settingsBox, 1, 1, 0);
$vbox->pack_start($buttonBox, 1, 1, 0);
$vbox->signal_connect(key_press_event => \&keypress);


my $window = Gtk2::Window->new;
$window->set_title ('Settings for '.$base);
$window->signal_connect (destroy => sub { Gtk2->main_quit; });
$window->add($vbox);
$window->set_resizable(0);
$window->show_all;

Gtk2->main;

sub keypress() {
  my ($widget, $event) = @_;
  if ($event->keyval() == 65307) {
    Gtk2->main_quit;
  }
}

sub button_remove {
  my $exec = $ENV{XDG_DATA_HOME}.'/uzbl/scripts/set_per_site_js_ext.pl';
  system($exec, $ARGV[0], $ARGV[1], $ARGV[2], $ARGV[3], $ARGV[4], $ARGV[5], $ARGV[6], $mode, 'remove');
  Gtk2->main_quit;
  1;
}

sub button_ok {
  if (not $found) {
    if($bydomain->get_active) { $mode='domain'; }
    if($byhost->get_active) { $mode='host'; }
    if($bydir->get_active) { $mode='dir'; }
  }

  my $res = ' s'.($scripts->get_active()?'1':'0').
	    ' p'.($plugins->get_active()?'1':'0').
	    ' i'.($images->get_active()?'1':'0').
	    #' r'.($scripts->get_active()?'1':'0')
	    ' x'.(($proxy->get_text() eq '')?'0':$proxy->get_text());

  # TODO: use same path as this script like in follow_dmenu
  my $exec = ($ENV{XDG_DATA_HOME} ? $ENV{XDG_DATA_HOME} : $ENV{HOME}.'/.local/share').'/uzbl/scripts/set_per_site_js_ext.pl';

  system($exec, $ARGV[0], $ARGV[1], $ARGV[2], $ARGV[3], $ARGV[4], $ARGV[5], $ARGV[6], $mode, $res);

  Gtk2->main_quit;
  1;
}

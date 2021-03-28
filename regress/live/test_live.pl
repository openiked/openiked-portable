#!/usr/bin/perl

# Copyright (c) 2020 - 2021 Tobias Heider <tobhe@openbsd.org>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

use strict;
use Getopt::Std;

my %options=();
getopts("hlt:s", \%options);

usage() if defined $options{h};

my $LEFT_SSH = $ENV{'LEFT_SSH'};
my $RIGHT_SSH = $ENV{'RIGHT_SSH'};
my $LEFT_ADDR = $ENV{'LEFT_ADDR'};
my $RIGHT_ADDR = $ENV{'RIGHT_ADDR'};
if (!defined $LEFT_SSH or
    !defined $RIGHT_SSH or
    !defined $LEFT_ADDR or
    !defined $RIGHT_ADDR) {
	usage();
}

my %left = ();
my %right = ();
$left{'ssh'} = $LEFT_SSH;
$right{'ssh'} = $RIGHT_SSH;

$left{'addr'} = $LEFT_ADDR;
$right{'addr'} = $RIGHT_ADDR;

$right{'name'} = "right";
$left{'name'} = "left";

my $tests = {
  "test_ping" => \&test_ping,
  "test_fragmentation" => \&test_fragmentation,
};

if (defined $options{l}) {
	print "tests:\n";
	for (keys %$tests) {
		print "\t" . $_ . "\n";
	}
	exit 0;
}

my $BUILDDIR = ($ENV{'BUILDDIR'} eq "") ? "obj" : $ENV{'BUILDDIR'};
if (-e $BUILDDIR and !-d $BUILDDIR) {
	print "error: BUILDDIR is not a directory\n";
	exit 1;
}
if (!-e $BUILDDIR) {
	mkdir $BUILDDIR;
}

init_osdep(\%left);
init_osdep(\%right);

if (defined $options{s}) {
	# Generate CAs and certs
	setup_ca("ca-right");
	setup_ca("ca-left");
	setup_ca("ca-both");
	setup_key("left");
	setup_key("right");
	setup_cert("left", "ca-both");
	setup_cert("right", "ca-both");
	setup_cert("left", "ca-left");
	setup_cert("right", "ca-right");
	deploy_certs();
	exit 0;
}

if (defined $options{t}) {
	if (defined $tests->{$options{t}}) {
		$tests->{$options{t}}->();
		cleanup();
		exit 0;
	}
	print "error: no such test " . $options{t} . "\n";
	exit 1;
}

# run all tests
for (keys %$tests) {
	$tests->{$_}->();
}

cleanup();
exit 0;

sub usage {
	print <<~DOC;
	usage: LEFT_SSH= RIGHT_SSH= LEFT_ADDR= RIGHT_ADDR= test_live.pl [-hls] [-t test]
		-h		Print usage
		-l		List tests
		-s		Setup CAs and certs
		-t "name"	Run subtest "name"
	DOC
	exit 1;
}

sub test_ping {
	my $sub_name = (caller(0))[3];
	my %lconf = (
		'from' => $left{'addr'},
		'to' => $right{'addr'},
		'srcid' => "$left{'name'}-from-ca-both",
		'mode' => "active",
	);
	setup_config("$sub_name-left", \%lconf);
	system("chmod 0600 $BUILDDIR/$sub_name-left.conf");
	system <<~DOC;
	echo "cd /tmp
	put $BUILDDIR/$sub_name-left.conf test.conf" | sftp -q $left{'ssh'} -q;
	DOC

	my %rconf = (
		'from' => $right{'addr'},
		'to' => $left{'addr'},
		'srcid' => "$right{'name'}-from-ca-both",
		'mode' => "active",
	);
	setup_config("$sub_name-right", \%rconf);
	system("chmod 0600 $BUILDDIR/$sub_name-right.conf");
	system <<~DOC;
	echo "cd /tmp
	put $BUILDDIR/$sub_name-right.conf test.conf" | sftp -q $right{'ssh'} -q;
	DOC

	setup_start(\%left);
	setup_start(\%right);

	print "FAIL" if check_ping(\%left, \%right);
}

sub test_fragmentation {
	my $sub_name = (caller(0))[3];
	my %lconf = (
		'from' => $left{'addr'},
		'to' => $right{'addr'},
		'fragmentation' => 1,
		'srcid' => "$left{'name'}-from-ca-both",
		'mode' => "active",
	);
	setup_config("$sub_name-left", \%lconf);
	system("chmod 0600 $BUILDDIR/$sub_name-left.conf");
	system <<~DOC;
	echo "cd /tmp
	put $BUILDDIR/$sub_name-left.conf test.conf" | sftp -q $left{'ssh'} -q;
	DOC

	my %rconf = (
		'from' => $right{'addr'},
		'to' => $left{'addr'},
		'fragmentation' => 1,
		'srcid' => "$right{'name'}-from-ca-both",
		'mode' => "active",
	);
	setup_config("$sub_name-right", \%rconf);
	system("chmod 0600 $BUILDDIR/$sub_name-right.conf");
	system <<~DOC;
	echo "cd /tmp
	put $BUILDDIR/$sub_name-right.conf test.conf" | sftp -q $right{'ssh'} -q;
	DOC

	setup_start(\%left);
	setup_start(\%right);
	print "FAIL" if check_ping(\%left, \%right);
}

sub cleanup {
	print("Cleaning up.\n");
	setup_stop(\%left);
	setup_stop(\%right);
	system("ssh -q $left{'ssh'} \"rm /tmp/test.conf\" 2>/dev/null");
	system("ssh -q $right{'ssh'} \"rm /tmp/test.conf\" 2>/dev/null");
}

sub setup_start {
	my ($peer) = @_;
	system("ssh -q $peer->{'ssh'} \"$peer->{'cmd_flush'}; pkill iked; ".
	    "iked -df /tmp/test.conf\&\"\&");
}

sub setup_stop {
	my ($peer) = @_;
	system("ssh -q $peer->{'ssh'} \"$peer->{'cmd_flush'}; pkill iked;\" ".
	    "2>/dev/null");
}

sub setup_ca {
	my ($ca) = @_;
	setup_key($ca);
	system("openssl req -subj \"/C=DE/ST=Bavaria/L=Munich/O=iked/CN=$ca\" ".
	    "-new -x509 -key $BUILDDIR/$ca.key -out $BUILDDIR/$ca.crt");
}

sub setup_key {
	my ($name) = @_;
	system("openssl genrsa -out $BUILDDIR/$name.key 2048");
	system("openssl rsa -in $BUILDDIR/$name.key -pubout > $BUILDDIR/$name.pub");
}

sub setup_cert {
	my ($name, $ca) = @_;

	open(my $dest, '>', "$BUILDDIR/$name-from-$ca.cnf") || die $!;
	open(my $src, '<', "crt.in") || die $!;
	print $dest "ALTNAME = $name-from-$ca\n";
	while(<$src>){
		print $dest $_;
	}
	close($dest);
	close($src);

	system("openssl req -config $BUILDDIR/$name-from-$ca.cnf -new ".
	    "-key $BUILDDIR/$name.key -nodes " .
	    "-out $BUILDDIR/$name-from-$ca.csr");

	system("openssl x509 -extfile $BUILDDIR/$name-from-$ca.cnf ".
	    "-extensions req_cert_extensions -req ".
	    "-in $BUILDDIR/$name-from-$ca.csr ".
	    "-CA $BUILDDIR/$ca.crt -CAkey $BUILDDIR/$ca.key ".
	    "-CAcreateserial -out $BUILDDIR/$name-from-$ca.crt");
}

sub setup_config {
	my ($name, $conf) = @_;

	open(my $dest, '>', "$BUILDDIR/$name.conf") || die $!;
	open(my $src, '<', "iked.in") || die $!;

	my $globals = "";
	if (defined $conf->{'fragmentation'}) {
		$globals = $globals . "set fragmentation\n";
	}
	if (defined $conf->{'singleikesa'}) {
		$globals = $globals . "set enforcesingleikesa\n";
	}

	print $dest "FROM=\"$conf->{'from'}\"\n";
	print $dest "TO=\"$conf->{'to'}\"\n";
	print $dest "PEER_ADDR=\"$conf->{'to'}\"\n";
	print $dest "MODE=\"$conf->{'mode'}\"\n";
	print $dest "TMODE=\"$conf->{'tmode'}\"\n";
	print $dest "AUTH=\"$conf->{'auth'}\"\n";
	print $dest "IPCOMP=\"$conf->{'ipcomp'}\"\n";
	print $dest "SRCID=\"$conf->{'srcid'}\"\n";
	print $dest "DSTID=\"$conf->{'dstid'}\"\n";
	print $dest "CONFIG=\"\"\n";

	print $dest "$globals";

	while(<$src>){
		print $dest $_;
	}
}

# XXX: needs less globals
sub deploy_certs {
	system <<~DOC;
	echo "cd $left{'etc_dir'}\n
	put $BUILDDIR/left-from-ca-both.crt certs\n
	put $BUILDDIR/left-from-ca-right.crt certs\n
	put $BUILDDIR/left.key private/local.key\n
	put $BUILDDIR/left.pub local.pub\n
	put $BUILDDIR/ca-left.crt ca\n
	put $BUILDDIR/ca-both.crt ca\n" | sftp -q $left{'ssh'} -q;
	DOC

	system <<~"DOC";
	echo "cd $right{'etc_dir'}\n
	put $BUILDDIR/right-from-ca-both.crt certs\n
	put $BUILDDIR/right-from-ca-right.crt certs\n
	put $BUILDDIR/right.key private/local.key\n
	put $BUILDDIR/right.pub local.pub\n
	put $BUILDDIR/ca-right.crt ca\n
	put $BUILDDIR/ca-both.crt ca\n" | sftp -q $right{'ssh'} -q;
	DOC
}

sub init_osdep {
	my ($peer) = @_;

	$peer->{'os'} = `ssh -q $peer->{'ssh'} uname`;
	$peer->{'etc_dir'} = "/etc/iked";
	if ($peer->{'os'} cmp "OpenBSD") {
		$peer->{'cmd_flush'} = "ipsecctl -F";
	} elsif ($peer->{'os'} cmp "Linux") {
		$peer->{'cmd_flush'} = "ip x p f; ip x s f";
	} elsif ($peer->{'os'} cmp "FreeBSD") {
		$peer->{'cmd_flush'} = "setkey -PD; setkey -D";
		$peer->{'etc_dir'} = "/usr/local/etc/iked";
	} else {
		print "error: unsupported OS " . $peer->{'os'} ."\n";
		exit 1;
	}
}

sub check_ping {
	my ($left, $right) = @_;

	my $out = `ssh -q $left->{'ssh'} \"tcpdump -n -c2 -i enc0 \\
	    -w /tmp/test.pcap > /dev/null& \\
	    ping -w 1 -n -c 5 $right->{'addr'} > /dev/null; \\
	    tcpdump -n -r /tmp/test.pcap; rm -f /tmp/test.pcap; \\
	    kill -9 \\\$! > /dev/null 2>&1 || true\"`;
	my $ltor = ($out =~ /\(authentic,confidential\):\sSPI\s0x[0-9a-f]{8}:\s
	    $right->{'addr'}\s>\s$left->{'addr'}/x);
	my $rtol = ($out =~ /\(authentic,confidential\):\sSPI\s0x[0-9a-f]{8}:\s
	    $left->{'addr'}\s>\s$right->{'addr'}/x);
	if ($ltor && $rtol) {
		print "Ping werks\n";
		return 0;
	}
	print "Ping failed\n";
	return 1;
}

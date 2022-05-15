#!/usr/bin/perl

# Copyright (c) 2020 - 2022 Tobias Heider <tobhe@openbsd.org>
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

my @tests = (
  "test_single_ca",
  "test_single_ca_asn1dn",
  "test_altname",
  "test_multi_ca",
  "test_no_ca",
  "test_pubkey",
  "test_psk",
  "test_invalid_ke",
  "test_ikesa_all",
  "test_childsa_all",
  "test_group_sntrup761x25519",
  "test_transport",
  "test_fragmentation",
  "test_singleikesa",
  "test_config_addr",
  "test_config_addrpool",
  "test_lifetime",
  "test_dstid_fail",
);

if (defined $options{l}) {
	print("tests:\n");
	for my $test (@tests) {
		print("\t$test\n");
	}
	exit 0;
}

my $BUILDDIR = (defined $ENV{'BUILDDIR'}) ? $ENV{'BUILDDIR'} : "obj";
if (-e $BUILDDIR and !-d $BUILDDIR) {
	print("error: BUILDDIR is not a directory\n");
	exit 1;
}
if (!-e $BUILDDIR) {
	mkdir $BUILDDIR;
}

init_osdep(\%left);
init_osdep(\%right);

if (defined $options{s}) {
	# Generate CAs and certs
	setup_ca("ca-none");
	setup_ca("ca-right");
	setup_ca("ca-left");
	setup_ca("ca-both");

	setup_key("left");
	setup_key("right");

	setup_cert("left", "ca-both");
	setup_cert("right", "ca-both");

	setup_cert("right", "ca-left");
	setup_cert("left", "ca-right");

	setup_cert("right", "ca-none");
	setup_cert("left", "ca-none");
	deploy_certs();
	exit 0;
}

if (defined $options{t}) {
	if ( grep { $options{t} eq $_ } @tests ) {
		print("$options{t}: ");
		eval "$options{t}()";
		cleanup();
		exit 0;
	}
	print("error: no such test $options{t}\n");
	exit 1;
}

# run all tests
for my $test (@tests) {
	print("$test: ");
	eval "$test()";
	cleanup();
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

sub test_single_ca {
	my %lconf = (
		'from' => $left{'addr'},
		'to' => $right{'addr'},
		'srcid' => "$left{'name'}-from-ca-both",
		'mode' => "active",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $right{'addr'}"],
	);
	my %rconf = (
		'from' => $right{'addr'},
		'to' => $left{'addr'},
		'srcid' => "$right{'name'}-from-ca-both",
		'mode' => "active",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $left{'addr'}"],
	);
	test_basic(\%lconf, \%rconf);
}

sub test_single_ca_asn1dn {
	my %lconf = (
		'from' => $left{'addr'},
		'to' => $right{'addr'},
		'srcid' => "\\\"/C=DE/ST=Bavaria/L=Munich/O=iked/CN=$left{'name'}-from-ca-both\\\"",
		'mode' => "active",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $right{'addr'}"],
	);
	my %rconf = (
		'from' => $right{'addr'},
		'to' => $left{'addr'},
		'srcid' => "\\\"/C=DE/ST=Bavaria/L=Munich/O=iked/CN=$right{'name'}-from-ca-both\\\"",
		'mode' => "active",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $left{'addr'}"],
	);
	test_basic(\%lconf, \%rconf);
}

sub test_altname {
	my %lconf = (
		'from' => $left{'addr'},
		'to' => $right{'addr'},
		'srcid' => "$left{'name'}-from-ca-both-alternative",
		'mode' => "active",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $right{'addr'}"],
	);
	my %rconf = (
		'from' => $right{'addr'},
		'to' => $left{'addr'},
		'srcid' => "$right{'name'}-from-ca-both\@openbsd.org",
		'mode' => "active",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $left{'addr'}"],
	);
	test_basic(\%lconf, \%rconf);
}

sub test_multi_ca {
	my %lconf = (
		'from' => $left{'addr'},
		'to' => $right{'addr'},
		'srcid' => "$left{'name'}-from-ca-right",
		'mode' => "active",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $right{'addr'}"],
	);
	my %rconf = (
		'from' => $right{'addr'},
		'to' => $left{'addr'},
		'srcid' => "$right{'name'}-from-ca-left",
		'mode' => "active",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $left{'addr'}"],
	);
	test_basic(\%lconf, \%rconf);
}

sub test_no_ca {
	my %lconf = (
		'from' => $left{'addr'},
		'to' => $right{'addr'},
		'srcid' => "$left{'name'}-from-ca-none",
		'mode' => "active",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $right{'addr'}"],
	);
	my %rconf = (
		'from' => $right{'addr'},
		'to' => $left{'addr'},
		'srcid' => "$right{'name'}-from-ca-none",
		'mode' => "active",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $left{'addr'}"],
	);
	test_basic(\%lconf, \%rconf);
}

sub test_pubkey {
	my %lconf = (
		'from' => $left{'addr'},
		'to' => $right{'addr'},
		'srcid' => "$left{'name'}-pub",
		'mode' => "active",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $right{'addr'}"],
	);
	my %rconf = (
		'from' => $right{'addr'},
		'to' => $left{'addr'},
		'srcid' => "$right{'name'}-pub",
		'mode' => "active",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $left{'addr'}"],
	);
	test_basic(\%lconf, \%rconf);
}

sub test_psk {
	my %lconf = (
		'from' => $left{'addr'},
		'to' => $right{'addr'},
		'srcid' => "$left{'name'}-psk",
		'auth' => "psk mekmitasdigoat",
		'mode' => "active",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $right{'addr'}"],
	);
	my %rconf = (
		'from' => $right{'addr'},
		'to' => $left{'addr'},
		'srcid' => "$right{'name'}-psk",
		'auth' => "psk mekmitasdigoat",
		'mode' => "active",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $left{'addr'}"],
	);
	test_basic(\%lconf, \%rconf);
}

sub test_group_sntrup761x25519 {
	my %lconf = (
		'from' => $left{'addr'},
		'to' => $right{'addr'},
		'srcid' => "$left{'name'}-from-ca-both",
		'mode' => "active",
		'ikesa' => "ikesa group sntrup761x25519",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $right{'addr'}.*group sntrup761x25519"],
	);
	my %rconf = (
		'from' => $right{'addr'},
		'to' => $left{'addr'},
		'srcid' => "$right{'name'}-from-ca-both",
		'mode' => "active",
		'ikesa' => "ikesa group sntrup761x25519",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $left{'addr'}.*group sntrup761x25519"],
	);
	test_basic(\%lconf, \%rconf);
}

sub test_ikesa_all {
	my %lconf = (
		'from' => $left{'addr'},
		'to' => $right{'addr'},
		'srcid' => "$left{'name'}-from-ca-both",
		'mode' => "active",
		'ikesa' => "ikesa group curve25519 group brainpool512" .
		    " group brainpool384 group brainpool256 group brainpool224" .
		    " group ecp224 group ecp192 group ecp521 group ecp384" .
		    " group ecp256 group modp8192 group modp6144 group modp4096" .
		    " group modp3072 group modp2048 group modp1536 group modp1024" .
		    " group modp768 group sntrup761x25519" .
		    " enc aes-256-gcm-12 enc aes-128-gcm-12 enc aes-256-gcm" .
		    " enc aes-128-gcm" .
		    " ikesa group curve25519 group brainpool512" .
		    " group brainpool384 group brainpool256 group brainpool224" .
		    " group ecp224 group ecp192 group ecp521 group ecp384" .
		    " group ecp256 group modp8192 group modp6144 group modp4096" .
		    " group modp3072 group modp2048 group modp1536 group modp1024" .
		    " group modp768 group sntrup761x25519" .
		    " enc aes-256 enc aes-192 enc aes-128 enc 3des" .
		    " auth hmac-sha2-512 auth hmac-sha2-384 auth hmac-sha2-256" .
		    " auth hmac-sha1 auth hmac-md5",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $right{'addr'}"],
	);
	my %rconf = (
		'from' => $right{'addr'},
		'to' => $left{'addr'},
		'srcid' => "$right{'name'}-from-ca-both",
		'mode' => "active",
		'ikesa' => "ikesa group curve25519 group brainpool512" .
		    " group brainpool384 group brainpool256 group brainpool224" .
		    " group ecp224 group ecp192 group ecp521 group ecp384" .
		    " group ecp256 group modp8192 group modp6144 group modp4096" .
		    " group modp3072 group modp2048 group modp1536 group modp1024" .
		    " group modp768 group sntrup761x25519" .
		    " enc aes-256-gcm-12 enc aes-128-gcm-12 enc aes-256-gcm" .
		    " enc aes-128-gcm" .
		    " ikesa group curve25519 group brainpool512" .
		    " group brainpool384 group brainpool256 group brainpool224" .
		    " group ecp224 group ecp192 group ecp521 group ecp384" .
		    " group ecp256 group modp8192 group modp6144 group modp4096" .
		    " group modp3072 group modp2048 group modp1536 group modp1024" .
		    " group modp768 group sntrup761x25519" .
		    " enc aes-256 enc aes-192 enc aes-128 enc 3des" .
		    " auth hmac-sha2-512 auth hmac-sha2-384 auth hmac-sha2-256" .
		    " auth hmac-sha1 auth hmac-md5",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $left{'addr'}"],
	);
	test_basic(\%lconf, \%rconf);
}

sub test_childsa_all {
	my %lconf = (
		'from' => $left{'addr'},
		'to' => $right{'addr'},
		'srcid' => "$left{'name'}-from-ca-both",
		'mode' => "active",
		'ikesa' => "childsa group curve25519 group brainpool512" .
		    " group brainpool384 group brainpool256 group brainpool224" .
		    " group ecp224 group ecp192 group ecp521 group ecp384" .
		    " group ecp256 group modp8192 group modp6144 group modp4096" .
		    " group modp3072 group modp2048 group modp1536 group modp1024" .
		    " group modp768 group sntrup761x25519 group none" .
		    " enc chacha20-poly1305 enc aes-256-gcm enc aes-192-gcm" .
		    " enc aes-128-gcm noesn esn" .
		    " childsa group curve25519 group brainpool512" .
		    " group brainpool384 group brainpool256 group brainpool224" .
		    " group ecp224 group ecp192 group ecp521 group ecp384" .
		    " group ecp256 group modp8192 group modp6144 group modp4096" .
		    " group modp3072 group modp2048 group modp1536 group modp1024" .
		    " group modp768 group sntrup761x25519 group none" .
		    " enc aes-256 enc aes-192 enc aes-128 enc 3des enc aes-256-ctr" .
		    " enc aes-192-ctr enc aes-128-ctr enc cast enc blowfish" .
		    " auth hmac-sha2-512 auth hmac-sha2-384 auth hmac-sha2-256 auth hmac-sha1" .
		    " auth hmac-md5 " .
		    " prf hmac-sha2-512 prf hmac-sha2-384 prf hmac-sha2-256 prf hmac-sha1" .
		    " prf hmac-md5 " .
		    " noesn esn",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $right{'addr'}"],
	);
	my %rconf = (
		'from' => $right{'addr'},
		'to' => $left{'addr'},
		'srcid' => "$right{'name'}-from-ca-both",
		'mode' => "active",
		'ikesa' => "childsa group curve25519 group brainpool512" .
		    " group brainpool384 group brainpool256 group brainpool224" .
		    " group ecp224 group ecp192 group ecp521 group ecp384" .
		    " group ecp256 group modp8192 group modp6144 group modp4096" .
		    " group modp3072 group modp2048 group modp1536 group modp1024" .
		    " group modp768 group sntrup761x25519 group none" .
		    " enc chacha20-poly1305 enc aes-256-gcm enc aes-192-gcm" .
		    " enc aes-128-gcm noesn esn" .
		    " childsa group curve25519 group brainpool512" .
		    " group brainpool384 group brainpool256 group brainpool224" .
		    " group ecp224 group ecp192 group ecp521 group ecp384" .
		    " group ecp256 group modp8192 group modp6144 group modp4096" .
		    " group modp3072 group modp2048 group modp1536 group modp1024" .
		    " group modp768 group sntrup761x25519 group none" .
		    " enc aes-256 enc aes-192 enc aes-128 enc 3des enc aes-256-ctr" .
		    " enc aes-192-ctr enc aes-128-ctr enc cast enc blowfish" .
		    " auth hmac-sha2-512 auth hmac-sha2-384 auth hmac-sha2-256 auth hmac-sha1" .
		    " auth hmac-md5 " .
		    " prf hmac-sha2-512 prf hmac-sha2-384 prf hmac-sha2-256 prf hmac-sha1" .
		    " prf hmac-md5 " .
		    " noesn esn",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $left{'addr'}"],
	);
	test_basic(\%lconf, \%rconf);
}

sub test_invalid_ke {
	my %lconf = (
		'from' => $left{'addr'},
		'to' => $right{'addr'},
		'srcid' => "$left{'name'}-from-ca-both",
		'mode' => "passive",
		'ikesa' => "ikesa group curve25519",
		'expect' => [
		    "want dh CURVE25519, KE has ECP_256",
		    "failed to negotiate IKE SA",
		    "spi=0x[0-9a-f]{16}: established peer $right{'addr'}"
		],
	);
	my %rconf = (
		'from' => $right{'addr'},
		'to' => $left{'addr'},
		'srcid' => "$right{'name'}-from-ca-both",
		'mode' => "active",
		'ikesa' => "ikesa group ecp256 group curve25519",
		'expect' => [
		    "reinitiating with new DH group",
		    "spi=0x[0-9a-f]{16}: established peer $left{'addr'}",
		],
	);
	test_basic(\%lconf, \%rconf);
}

sub test_config_addr {
	my %lconf = (
		'from' => $left{'addr'},
		'to' => $right{'addr'},
		'srcid' => "$left{'name'}-from-ca-both",
		'mode' => "passive",
		'config' => "config address 172.16.13.36",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $right{'addr'}"],
	);
	my %rconf = (
		'from' => $right{'addr'},
		'to' => $left{'addr'},
		'srcid' => "$right{'name'}-from-ca-both",
		'mode' => "active",
		'config' => "request address any",
		'expect' => [
		    "spi=0x[0-9a-f]{16}: established peer $left{'addr'}",
		    "obtained lease: 172.16.13.36"
		],
	);
	test_basic(\%lconf, \%rconf);
}

sub test_config_addrpool {
	my %lconf = (
		'from' => $left{'addr'},
		'to' => $right{'addr'},
		'srcid' => "$left{'name'}-from-ca-both",
		'mode' => "passive",
		'config' => "config address 172.16.13.36/24",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $right{'addr'}"],
	);
	my %rconf = (
		'from' => $right{'addr'},
		'to' => $left{'addr'},
		'srcid' => "$right{'name'}-from-ca-both",
		'mode' => "active",
		'config' => "request address any",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $left{'addr'}"],
	);
	test_basic(\%lconf, \%rconf);
}

sub test_transport {
	my %lconf = (
		'from' => $left{'addr'},
		'to' => $right{'addr'},
		'tmode' => "transport",
		'srcid' => "$left{'name'}-from-ca-both",
		'mode' => "active",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $right{'addr'}"],
	);
	my %rconf = (
		'from' => $right{'addr'},
		'to' => $left{'addr'},
		'tmode' => "transport",
		'srcid' => "$right{'name'}-from-ca-both",
		'mode' => "active",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $left{'addr'}"],
	);
	test_basic(\%lconf, \%rconf);
}

sub test_fragmentation {
	my %lconf = (
		'from' => $left{'addr'},
		'to' => $right{'addr'},
		'fragmentation' => 1,
		'srcid' => "$left{'name'}-from-ca-both",
		'mode' => "active",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $right{'addr'}"],
	);
	my %rconf = (
		'from' => $right{'addr'},
		'to' => $left{'addr'},
		'fragmentation' => 1,
		'srcid' => "$right{'name'}-from-ca-both",
		'mode' => "active",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $left{'addr'}"],
	);
	test_basic(\%lconf, \%rconf);
}

sub test_singleikesa {
	my %lconf = (
		'from' => $left{'addr'},
		'to' => $right{'addr'},
		'singleikesa' => 1,
		'srcid' => "$left{'name'}-from-ca-both",
		'mode' => "active",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $right{'addr'}"],
	);
	my %rconf = (
		'from' => $right{'addr'},
		'to' => $left{'addr'},
		'singleikesa' => 1,
		'srcid' => "$right{'name'}-from-ca-both",
		'mode' => "active",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $left{'addr'}"],
	);
	test_basic(\%lconf, \%rconf);
}

sub test_lifetime {
	my %lconf = (
		'from' => $left{'addr'},
		'to' => $right{'addr'},
		'srcid' => "$left{'name'}-from-ca-both",
		'mode' => "active",
		'config' => "ikelifetime 30 lifetime 20 bytes 500K",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $right{'addr'}"],
	);
	my %rconf = (
		'from' => $right{'addr'},
		'to' => $left{'addr'},
		'srcid' => "$right{'name'}-from-ca-both",
		'mode' => "active",
		'config' => "ikelifetime 30 lifetime 20 bytes 500K",
		'expect' => ["spi=0x[0-9a-f]{16}: established peer $left{'addr'}"],
	);
	# XXX: wait and check rekey log message
	test_basic(\%lconf, \%rconf);
}

sub test_dstid_fail {
	my %lconf = (
		'from' => $left{'addr'},
		'to' => $right{'addr'},
		'srcid' => "$left{'name'}-from-ca-both",
		'mode' => "active",
		'expect' => ["spi=0x[0-9a-f]{16}: sa_free: authentication failed notification from peer"],
	);
	my %rconf = (
		'from' => $right{'addr'},
		'to' => $left{'addr'},
		'srcid' => "$right{'name'}-from-ca-both",
		'dstid' => "dstid INVALID_ID",
		'mode' => "passive",
		'expect' => ["spi=0x[0-9a-f]{16}: ikev2_ike_auth_recv: no compatible policy found"],
	);
	test_basic(\%lconf, \%rconf);
}

sub test_basic {
	my ($lconf, $rconf) = @_;
	my $sub_name = (caller(0))[3];
	setup_config("$sub_name-left", $lconf);
	system("chmod 0600 $BUILDDIR/$sub_name-left.conf");
	system <<~DOC;
	echo "rename $left{'etc_dir'}/iked.conf $left{'etc_dir'}/iked.conf.old
	put $BUILDDIR/$sub_name-left.conf $left{'etc_dir'}/iked.conf" | sftp -q $left{'ssh'} -q > /dev/null;
	DOC

	setup_config("$sub_name-right", $rconf);
	system("chmod 0600 $BUILDDIR/$sub_name-right.conf");
	system <<~DOC;
	echo "rename $right{'etc_dir'}/iked.conf $right{'etc_dir'}/iked.conf.old
	put $BUILDDIR/$sub_name-right.conf $right{'etc_dir'}/iked.conf" | sftp -q $right{'ssh'} -q > /dev/null;
	DOC

	setup_start(\%left);
	setup_start(\%right);

	for (1..5) {
		sleep(1);
		if (check_log(\%left, $lconf->{'expect'}) &&
		    check_log(\%right, $rconf->{'expect'})) {
			print("SUCCESS\n");
			return;
		}
	}
	print("FAIL\n");
}

sub cleanup {
	setup_stop(\%left);
	setup_stop(\%right);
	system("ssh -q $left{'ssh'} \"mv $left{'etc_dir'}/iked.conf.old $left{'etc_dir'}/iked.conf; cat /tmp/test.log >> /tmp/iked-live.log; rm /tmp/test.log\" 2>/dev/null");
	system("ssh -q $right{'ssh'} \"mv $right{'etc_dir'}/iked.conf.old $right{'etc_dir'}/iked.conf; cat /tmp/test.log >> /tmp/iked-live.log; rm /tmp/test.log\" 2>/dev/null");
}

sub setup_start {
	my ($peer) = @_;
	system("ssh -q $peer->{'ssh'} \"$peer->{'cmd_flush'}; pkill iked; iked -dv 2> /tmp/test.log\&\"&")
}

sub setup_stop {
	my ($peer) = @_;
	system("ssh -q $peer->{'ssh'} \"$peer->{'cmd_flush'}; pkill iked\"");
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
	print $dest "IKESA=\"$conf->{'ikesa'}\"\n";
	print $dest "MODE=\"$conf->{'mode'}\"\n";
	print $dest "TMODE=\"$conf->{'tmode'}\"\n";
	print $dest "AUTH=\"$conf->{'auth'}\"\n";
	print $dest "IPCOMP=\"$conf->{'ipcomp'}\"\n";
	print $dest "SRCID=\"$conf->{'srcid'}\"\n";
	print $dest "DSTID=\"$conf->{'dstid'}\"\n";
	print $dest "CONFIG=\"$conf->{'config'}\"\n";

	print $dest "$globals";

	while(<$src>){
		print $dest $_;
	}
}

# XXX: needs less globals
sub deploy_certs {
	system <<~DOC;
	echo "cd $left{'etc_dir'}/iked\n
	mkdir certs\n
	put $BUILDDIR/left-from-ca-both.crt certs/\n
	put $BUILDDIR/left-from-ca-right.crt certs/\n
	put $BUILDDIR/left-from-ca-none.crt certs/\n
	put $BUILDDIR/right-from-ca-none.crt certs/\n
	mkdir private\n
	put $BUILDDIR/left.key private/local.key\n
	put $BUILDDIR/left.pub local.pub\n
	mkdir pubkeys\n
	mkdir pubkeys/fqdn\n
	put $BUILDDIR/right.pub pubkeys/fqdn/right-pub\n
	mkdir ca\n
	put $BUILDDIR/ca-left.crt ca/\n
	put $BUILDDIR/ca-both.crt ca/\n" | sftp -q $left{'ssh'} -q > /dev/null;
	DOC

	system <<~"DOC";
	echo "cd $right{'etc_dir'}/iked\n
	mkdir certs\n
	put $BUILDDIR/right-from-ca-both.crt certs/\n
	put $BUILDDIR/right-from-ca-left.crt certs/\n
	put $BUILDDIR/right-from-ca-none.crt certs/\n
	put $BUILDDIR/left-from-ca-none.crt certs/\n
	mkdir private\n
	put $BUILDDIR/right.key private/local.key\n
	put $BUILDDIR/right.pub local.pub\n
	mkdir pubkeys\n
	mkdir pubkeys/fqdn\n
	put $BUILDDIR/left.pub pubkeys/fqdn/left-pub\n
	mkdir ca\n
	put $BUILDDIR/ca-right.crt ca/\n
	put $BUILDDIR/ca-both.crt ca/\n" | sftp -q $right{'ssh'} -q > /dev/null;
	DOC
}

sub init_osdep {
	my ($peer) = @_;

	$peer->{'os'} = `ssh -q $peer->{'ssh'} uname`;
	$peer->{'etc_dir'} = "/etc";
	if (($peer->{'os'} cmp "OpenBSD\n") == 0) {
		$peer->{'cmd_flush'} = "ipsecctl -F";
	} elsif (($peer->{'os'} cmp "Linux\n") == 0) {
		$peer->{'cmd_flush'} = "ip x p f; ip x s f";
	} elsif (($peer->{'os'} cmp "FreeBSD\n") == 0) {
		$peer->{'cmd_flush'} = "setkey -PF; setkey -F";
		$peer->{'etc_dir'} = "/usr/local/etc";
	} else {
		print("error: unsupported OS $peer->{'os'}\n");
		exit 1;
	}
}

sub check_log {
	my ($peer, $res) = @_;
	my $log = `ssh -q $peer->{'ssh'} \"cat /tmp/test.log\"`;
	foreach my $regex (@$res) {
		return 0 if !($log =~ /$regex/);
	}
	return 1;
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
		print("Ping werks\n");
		return 0;
	}
	print("Ping failed\n");
	return 1;
}

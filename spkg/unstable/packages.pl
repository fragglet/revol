#!/usr/bin/perl
#

sub get_md5sum {
        my ($filename) = @_;

        open(MD5PIPE, "md5sum $filename |");

        my $sum = <MD5PIPE>;
	chomp $sum;

        close(MD5PIPE);

        die if !$sum;                                          
                                                               
        $sum =~ s/\s.*$//;

        return $sum;
}

open(PKGIN, "Packages.human") or die;
open(PKGOUT, ">Packages") or die;

while (<PKGIN>) {
	print PKGOUT $_;
	chomp;

	if (/^Filename\:/) {
		my ($filename) = /^Filename\:\s*(.*)/;

		die "$filename does not exist!" if !-e $filename;

		my @filestat = stat($filename);

		print PKGOUT "MD5sum: " . get_md5sum($filename) . "\n";
		print PKGOUT "Size: " . $filestat[7] . "\n";
	}
}

close(PKGOUT);
close(PKGIN);

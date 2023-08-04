#!/bin/bash

HEADER='
<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN"
"http://www.w3.org/TR/html4/loose.dtd">

<html>

<head>
<title>Revol: Linux on the Psion Revo</title>
</head>

<body bgcolor="white" text="black" link="black" vlink="black">
<h1>Revol</h1>
<a href="../">Main</a> |
<a href="../sshot.html">Screenshots</a> |
<a href="../faq.html">FAQ</a> |
<a href="../download.html">Downloads</a> |
<a href="../links.html">Links</a> |
<a href="../dev.html">Development Stuff</a> |
<a href="pkglist.html">Packages</a>
<hr>
'

FOOTER='
<hr>
<a href="mailto:fraggle@nospam.gmail.com">Simon Howard</a>. <br>
I am not associated with Psion Digital.

</body>
</html>
'

(echo "$HEADER"; ./pkglist; echo "$FOOTER") > pkgdesc/pkglist.html

pkgs=$(grep ^Package: spkg/unstable/Packages | sed "s/Package: //")

export pkg
for pkg in $pkgs; do
	(echo "$HEADER"; ./pkglist; echo "$FOOTER") > pkgdesc/$pkg.html
done


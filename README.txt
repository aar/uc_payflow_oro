// $Id: README.txt,v 1.1.2.2 2010/03/11 13:46:50 kwinters Exp $

Ubercart Payflow Pro Payment Gateway README

Open an issue at http://drupal.org/project/uc_payflowpro for support.

Code contributions from kwinters, mlutfy, and bountyx@gmail.com plus the original D5 version.

To install, place the uc_payflowpro in your modules folder. It will also work inside of the ubercart 
folder, but if you have to blow out your ubercart folder then keeping uc_payflowpro separate could 
make the process easier.

The location of the cacert file that comes with this module will work, but putting the file outside 
of a web-accessible place is recommended.  Our current recommendation is a path like 
/etc/ssl/certs/cacert.pem (which needs to be readable by the apache user, but writable only by root = 644).
This path may have to be absolute in some configurations.

Recurring billing functionality has not yet been tested in D6, so use at your own risk!

Below are the still-relevant notes from bountyx@gmail.com, who recently fixed the server 
locations and added the man-in-the-middle avoidance code for the D5 version (modified as 
needed to reflect recent changes).

03/17/2009 bountyx@gmail.com

Post-install:

You must specify the cert, or pem file for curl to validate certificate authorities. If you already
have a ca installed, simply specify the cacert.pem file (from the authority, or from curl's default
install). If you are on a webserver where you cannot gain access (or cant find it) to cacert.pem, you may use the
bundled cacert.pem file. It is reccomended that cacert.pem be placed OUTSIDE of the drupal, or
any directories accessible from the web. Once the cacert.pem file is placed somewhere on your machine,
in the Paypal PayFlow Pro settings, under "Certificate Path", specify the absolute or relative directory
of that cacert.pem file.

i.e. Certificate path can be:

../cacert.pem (one directory above drupal root dir)
../../cacert.pem (two directories above drupal root dir)
[Note: some configurations will require an absolute path.]

etc.

YOU MUST ENSURE THAT THIS FILE HAS PROPER READ PRIVLEDGES BY SERVICE
SO THAT IT IS ACCESSIBLE.

Just an fyi, the cacert.pem file covers most CA's. It is an export of the one used by Mozilla.
It was obtained from: http://curl.netmirror.org/docs/caextract.html

For more information relating security enhancements, see the following article:
http://ademar.name/blog/2006/04/curl-ssl-certificate-problem-v.html

ENJOY!

About:

This module has payflow pro Integration. It also allows for recurring payments using the Recurring Profile API of Payflow.

With this module, you can setup schedules for a product, and the product will be renew on that schedule.
There is also a full control panel for both the user AND and administrator to control their profiles and view payment history.
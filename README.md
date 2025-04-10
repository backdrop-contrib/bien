BIEN ~ Backdrop Import Export Nodes
----
BIEN allows you to export content from one Backdrop site to another. It provides
an Import form and an Export form. It places menu items for these to forms under
the `Configuration` menu item in the admin bar.


* `/admin/config/system/content`
  * Provides the import form. This form accepts a json file that was exported
  from a Backdrop site.
* `/admin/config/system/content/export`
  * Provides the export form. This form allows you to select which content types
  you would like to export and then creates a json file that is downloaded to
  your computer.

This module is still in development, we welcome testing and reporting of bugs or
feature requests in the issue queue. In our testing the module works fairly well
in the following controlled experiment. 

1) We use the [Sample Animal Content](https://github.com/backdrop-contrib/sample_animal_content) module to create an Animal content type,
related fields and 8 nodes of type Animal, including images.

2) We use BIEN to export the content type, the nodes, and the images and successfully
import them into a fresh site with any of those things in place.

We need to test a wider range of use cases.

Notes About Use of AI
---------------------
We have been using AI tools to make changes to this module and add features. We
encourage folks to review the code and help us identify quirky things that AI 
might have done or places where it may not be following BackdropCMS best practices.

Installation
------------

* Install this module using the official Backdrop CMS instructions at
    https://backdropcms.org/guide/modules

License
-------

This project is GPL v2 software. See the LICENSE.txt file in this directory for
complete text.

Current Maintainers
-------------------

* Tim Erickson ([@stpaultim](https://github.com/stpaultim)).
* Seeking additional maintainers.

Other Credits
-------------

* Ideal and initial work on this module by: Geoff St. Pierre [serundeputy](https://github.com/serundeputy).

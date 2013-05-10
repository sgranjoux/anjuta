<?xml version="1.0"?>
<anjuta>
    <plugin name="File Loader"
            url="http://anjuta.org/plugins/"
            mandatory="yes">
		<require group="Anjuta Plugin"
                 attribute="Interfaces"
                 value="IAnjutaFileLoader"/>
    </plugin>
    <plugin name="Document Manager"
            url="http://anjuta.org/plugins/"
            mandatory="yes">
		<require group="Anjuta Plugin"
                 attribute="Interfaces"
                 value="IAnjutaDocumentManager"/>
    </plugin>
    <filter>
		<require group="Anjuta Profile"
                 attribute="Editor"
                 value="yes"/>
    </filter>
</anjuta>

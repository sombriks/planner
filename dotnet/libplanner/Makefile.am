LIBRARY=	libplanner.dll
APIS=		libplanner-api.xml

INCLUDE_APIS=	
ASSEMBLIES=	-r glib-sharp.dll

EXTRA_DIST = 	*.custom *.metadata *.xml

gtksourceviewsharpdir = $(libdir)
gtksourceviewsharp_DATA = libplanner.dll

#FIXME
MCS = mcs
MONO = mono

all: $(LIBRARY)

generated-stamp: $(APIS) *.custom *.metadata
	$(GAPI_FIXUP) --api=libplanner-api.xml --metadata=libplanner.metadata && \
	$(MONO) $(GAPI_CODEGEN) --include $(INCLUDE_APIS) --generate $(APIS) \
	--outdir=generated --customdir=. --assembly-name=libplanner && touch generated-stamp

$(LIBRARY): generated-stamp
	$(MCS) --unsafe -nowarn:0660,0661 --target library $(ASSEMBLIES) --recurse '*.cs' -o $(LIBRARY)

install:
	for i in $(APIS); do						\
		$(INSTALL) -m 644 $$i @prefix@/share/gapi || true;	\
	done &&								\
	$(INSTALL) -m 644 $(LIBRARY) $(libdir)

clean:
	rm -rf $(LIBRARY) generated generated-stamp
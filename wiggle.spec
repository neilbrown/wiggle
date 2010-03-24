Summary: A tool for applying patches with conflicts
Name: wiggle
Version: 0.8
Release: 1
License: GPL
Group: Development/Tools 
URL: http://neil.brown.name/wiggle/
Source0: http://neil.brown.name/wiggle/%{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-buildroot

%description
Wiggle is a program for applying patches that 'patch' cannot
apply due to conflicting changes in the original.

Wiggle will always apply all changes in the patch to the original.
If it cannot find a way to cleanly apply a patch, it inserts it
in the original in a manner similar to 'merge', and report an
unresolvable conflict.

%prep
%setup -q

%build
make BINDIR=/usr/bin \
     MANDIR=%{_mandir} MAN1DIR=%{_mandir}/man1 MAN5DIR=%{_mandir}/man5 \
     CFLAGS="$RPM_OPT_FLAGS" \
     wiggle

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/usr/bin
mkdir -p $RPM_BUILD_ROOT%{_mandir}/man{1,5}

make BINDIR=$RPM_BUILD_ROOT/usr/bin \
     MANDIR=$RPM_BUILD_ROOT%{_mandir} \
     MAN1DIR=$RPM_BUILD_ROOT%{_mandir}/man1 \
     MAN5DIR=$RPM_BUILD_ROOT%{_mandir}/man5 \
     install

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
/usr/bin/wiggle
%{_mandir}/man1/wiggle.1*
%doc ANNOUNCE TODO notes
%doc p p.help


%changelog
* Thu May 22 2003 Horst von Brand <vonbrand@inf.utfsm.cl> 0.6-1 
- Initial build.



%global date COMMITDATE
%global shortcommit SCOMMITHASH

Name:		bond-slb-bpf
Version:	0
Release:	%{date}git%{shortcommit}%{?dist}
Summary:	bond slb using eBPF

License:	GPLv2
URL:		https://github.com/xdp-project/bpf-examples
Source0:	bpf-examples-%{shortcommit}.tar.gz

BuildRoot:	%(mktemp -ud %{_tmppath}/%{name}-%{version}-%release}-XXXXXX)

Conflicts:	bond-slb-nft
ExclusiveArch:	x86_64
BuildRequires:	make
BuildRequires:	gcc
BuildRequires:	llvm
BuildRequires:	clang >= 11
BuildRequires:	bpftool
BuildRequires:	pkgconf
BuildRequires:	pkgconf-pkg-config
BuildRequires:	pkgconfig(libbpf)
BuildRequires:	pkgconfig(libelf)
BuildRequires:	pkgconfig(zlib)
BuildRequires:	pkgconfig(libxdp)
BuildRequires:	glibc-devel.i686
BuildRequires:	kernel-headers

%description
Implements OVS bonding slb like functionality utilizing eBPF.

%prep
%autosetup -n %{name}-%{shortcommit}

%build
./configure
make -C pkt-loop-filter

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}/%{_libexecdir}/%{name}/
install -m 755 pkt-loop-filter/pkt-loop-filter %{buildroot}/%{_libexecdir}/%{name}/pkt-loop-filter
install -m 755 pkt-loop-filter/get-bond-active %{buildroot}/%{_libexecdir}/%{name}/get-bond-active
mkdir -p %{buildroot}/%{_prefix}/lib/NetworkManager/dispatcher.d/
sed -e "s:%%CONFIGFILE%%:%{_sysconfdif}/default/bond-slb:g" \
	-e "s:%%EXECFILTER%%:%{_libexecdir}/%{name}/pkt-loop-filter:g" \
	pkt-loop-filter/nm-dispatcher.sh >%{buildroot}/%{_prefix}/lib/NetworkManager/dispatcher.d/20-bond-slb.sh
chmod 0755 %{buildroot}/%{_prefix}/lib/NetworkManager/dispatcher.d/20-bond-slb.sh
mkdir -p %{buildroot}/%{_sysconfdif}/default/
install -m 644 pkt-loop-filter/default-config %{buildroot}/%{_sysconfdif}/default/bond-slb

%files
%{_sysconfdir}/default/bond-slb
%{_prefix}/lib/NetworkManager/dispatcher.d/20-bond-slb.sh
%{_libexecdir}/%{name}/pkt-loop-filter
%{_libexecdir}/%{name}/get-bond-active
%config(noreplace) %{_sysconfdif}/default/bond-slb

%changelog
* Tue Jul 26 2022 Jonathan Toppins <jtoppins@redhat.com>
- Initial release

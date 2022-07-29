%global date %COMMITDATE%
%global shortcommit %SHORTCOMMIT%

Name:		bond-slb-bpf
Version:	0
Release:	%{date}.g%{shortcommit}%{?dist}
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
BuildRequires:	kernel-headers

%description
Implements OVS bonding slb like functionality utilizing eBPF.

%prep
%autosetup -n bpf-examples-%{shortcommit}

%build
./configure
make -C pkt-loop-filter

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}/%{_libexecdir}/%{name}/
install -m 755 pkt-loop-filter/pkt-loop-filter %{buildroot}/%{_libexecdir}/%{name}/pkt-loop-filter
install -m 755 pkt-loop-filter/get-bond-active %{buildroot}/%{_libexecdir}/%{name}/get-bond-active
mkdir -p %{buildroot}/%{_prefix}/lib/NetworkManager/dispatcher.d/
sed -e "s:%%CONFIGFILE%%:%{_sysconfdir}/default/bond-slb:g" \
	-e "s:%%EXECFILTER%%:%{_libexecdir}/%{name}/pkt-loop-filter:g" \
	pkt-loop-filter/nm-dispatcher.sh >%{buildroot}/%{_prefix}/lib/NetworkManager/dispatcher.d/20-bond-slb.sh
chmod 0755 %{buildroot}/%{_prefix}/lib/NetworkManager/dispatcher.d/20-bond-slb.sh
mkdir -p %{buildroot}/%{_sysconfdir}/default/
install -m 644 pkt-loop-filter/default-config %{buildroot}/%{_sysconfdir}/default/bond-slb

%files
%{_prefix}/lib/NetworkManager/dispatcher.d/20-bond-slb.sh
%{_libexecdir}/%{name}/pkt-loop-filter
%{_libexecdir}/%{name}/get-bond-active
%config(noreplace) %{_sysconfdir}/default/bond-slb

%changelog
* Tue Jul 26 2022 Jonathan Toppins <jtoppins@redhat.com>
- Initial release

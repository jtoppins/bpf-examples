#!/usr/bin/env sh
#
# This is meant to be used as a NetworkManager-dispatcher script.
#
# Create a bond:
#
#   nmcli connection add con-name bond0 type bond ifname bond0 bond.options "mode=2,xmit_hash_policy=vlan+srcmac"
#   nmcli connection add con-name bond0-member1 type ethernet ifname <interface> master bond0
#   nmcli connection add con-name bond0-member2 type ethernet ifname <interface> master bond0
#   nmcli connection up bond0-member1
#   nmcli connection up bond0-member2
#   nmcli connection up bond0
#
# args: <interface> <nm_action>
set -e

CONFIGFILE=${CONFIGFILE:-%CONFIGFILE%}
EXECFILTER=${EXECFILTER:-%EXECFILTER%}

parse_args()
{
	NM_INTERFACE="$1"
	NM_ACTION="$2"
}

read_config_file()
{
	if test -f "${CONFIGFILE}"; then
		. ${CONFIGFILE}
	fi

	BOND_SLB_ENABLE=${BOND_SLB_ENABLE:-false}
	BOND_SLB_IFACES=${BOND_SLB_IFACES:-""}
	BOND_SLB_DEBUG=${BOND_SLB_DEBUG:-false}
}

sanity_check()
{
	if test "${BOND_SLB_ENABLE}" != "true"; then
		exit 0
	fi

	echo "${BOND_SLB_IFACES}" | \
		grep "\b${NM_INTERFACE}\b" 2>&1 >/dev/null || exit 0

	test "$NM_ACTION" = "up" || test "$NM_ACTION" = "down" || exit 0

	# verify the bond is using SRCMAC+VLAN hashing
	ip -d -o link show ${NM_INTERFACE} | \
		grep "xmit_hash_policy vlan+srcmac" 2>&1 >/dev/null || exit 0
	ip -d -o link show ${NM_INTERFACE} | \
		grep "mode balance-xor" 2>&1 >/dev/null || exit 0
}

parse_args "$@"
read_config_file
sanity_check

debug=""
if test "${BOND_SLB_DEBUG}" = "true"; then
	debug="--debug"
fi

case ${NM_ACTION} in
up)
	${EXECFILTER} ${NM_INTERFACE} ${debug}
	;;
down)
	${EXECFILTER} ${NM_INTERFACE} --unload ${debug}
	;;
esac

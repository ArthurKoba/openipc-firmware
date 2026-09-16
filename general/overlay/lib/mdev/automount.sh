#!/bin/sh

destdir=/mnt
hook=/usr/share/openipc/automount-hook

run_hook() {
	[ -x "$hook" ] || return 0
	"$hook" "$@" ||
		logger -s -p daemon.warning -t automount \
			"storage hook failed: action=$1 device=$2 mountpoint=$3"
}

my_umount() {
	if grep -qs "^/dev/$1 " /proc/mounts; then
		run_hook remove "/dev/$1" "${destdir}/$1"
		umount "${destdir}/$1" || return 1
	fi

	[ -d "${destdir}/$1" ] && rmdir "${destdir}/$1"
}

my_mount() {
	# Repeated coldplug scans must not remount an existing filesystem.
	if grep -qs "^/dev/$1 ${destdir}/$1 " /proc/mounts; then
		return 0
	fi
	mkdir -p "${destdir}/$1" || exit 1

	if ! mount -t auto "/dev/$1" "${destdir}/$1"; then
		# failed to mount, clean up mountpoint
		rmdir "${destdir}/$1"
		exit 1
	fi
	run_hook add "/dev/$1" "${destdir}/$1"

	# copy files from autoconfig folder
	[ -d "${destdir}/$1/autoconfig" ] && cp -afv ${destdir}/$1/autoconfig/* / | logger -s -p daemon.info -t autoconfig

	# execution of the specified commands one time
	[ -f "${destdir}/$1/autoconfig.sh" ] && (sh ${destdir}/$1/autoconfig.sh; rm -f ${destdir}/$1/autoconfig.sh) | logger -s -p daemon.info -t autoconfig

	# execution of the specified commands
	[ -f "${destdir}/$1/autostart.sh" ] && sh ${destdir}/$1/autostart.sh | logger -s -p daemon.info -t autostart
}

case "${ACTION}" in
	add|"")
		my_mount ${MDEV}
		;;

	remove)
		my_umount ${MDEV}
		;;
esac

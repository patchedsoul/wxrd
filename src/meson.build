
sources = [
	'main.c',
	'backend.c',
	'input.c',
	'view.c',
	'xdg-shell.c',
	'xwayland.c',
	'wxrd-renderer.c',
] + wl_protos_src + wl_protos_headers

executable(
	'wxrd',
	sources,
	dependencies: [
		wayland_server_dep,
		wayland_protocols_dep,
		wayland_client_dep,
		xrdesktop_dep,
		wlroots_dep,
		glesv2_dep,
		egl_dep,
		xkbcommon_dep,
	],
	include_directories: [src_inc],
	install: false)

# GNOME Session Manager

The GNOME session manager is in charge of starting the core components of the
GNOME desktop, and applications that should be launched at login time. This
module is also a natural place for various configuration files that define
important settings that are applied to the GNOME desktop, such as selecting
which xdg-desktop-portal backends to use.

You may download updates to the package from: http://download.gnome.org/sources/gnome-session/

You can also [view `gnome-session`'s documentation](https://gnome.pages.gitlab.gnome.org/gnome-session)

## Contributing

To discuss `gnome-session`, you should use the
[GNOME support forum](https://discourse.gnome.org/c/platform/).

`gnome-session` development happens on
[GNOME's GitLab](http://gitlab.gnome.org/GNOME/gnome-session).
You will need to create an account to contribute.

Bugs should be reported to the
[`gnome-session` issue tracker](https://gitlab.gnome.org/GNOME/gnome-session/-/issues/).
Please read the
[GNOME Handbook's Guidance](https://handbook.gnome.org/issues/reporting.html)
on how to prepare a useful bug report.

Patches can be contributed by
[opening a merge request](https://gitlab.gnome.org/GNOME/gnome-session/-/merge_requests).
Please read the
[GNOME Handbook's Guidance](https://handbook.gnome.org/development.html)
on how to prepare a successful merge request.

## Building and Installing

Before you can build `gnome-session`, you need the following dependencies:

- A C compiler
- Meson
- Ninja
- `json-glib`
- `systemd`
- `gtk4`
- `gnome-desktop4`

Once you have all the necessary dependencies, you can use Meson to build
`gnome-session`:

```
$ meson setup _build
$ meson compile -C_build
```

And finally, you can use Meson to install `gnome-session` to your system:

```
$ sudo meson install -C_build
```

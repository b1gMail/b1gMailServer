# b1gMailServer

This is the GPLv2 release of the formerly proprietary b1gMailServer add-on for b1gMail.

## Building
You can build the Linux installer package using the `build.sh` script, provided that you have a recent Docker version installed on your system.

```
./build.sh
```

You will find the built installer in `dist-files/`.

## Build Compatibility
You can easily upgrade a prior (commercial) b1gMailServer copy by just running the built installer and following the instructions.

Please note that the commercial version of b1gMailServer has been built on a fairly old CentOS build machine to improve compatibility with older glibc versions.

The Docker-based build system of the OSS version builds on Ubuntu Xenial (16.04). Hence, the produced installer might not work on older systems.

## Build Number
The build number used for the installer is taken from the file `src/buildno`. In the CI system used for the commercial version, the number in this file was auto-incremented whenever a built has been generated.

When making a new release based on the OSS version, the number in this file should be incremented manually, since an upgrade via the installer can only be performed when the build version is newer than the currently installed one.

## Disclaimer
I wrote the foundation and big parts of this code (especially POP3, IMAP support) when I was about 15 years old.

Even though I have improved many places which used to rely on raw pointers and low-level string manipulation over time, overall coding style, architecture, maintainability and code quality do not meet my current ideas of how modern C++ code should look like.

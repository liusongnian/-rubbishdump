#!/usr/bin/ruby

$qt = ""
#$qt = "/opt/Qt5.8.0"

require 'fileutils'

load 'version.rb'

$workdir = "_release"
$dstdir = $workdir + "/#{$product_name}"

$dstdir_iconengines = $dstdir + "/iconengines"
$dstdir_imageformats = $dstdir + "/imageformats"
$dstdir_platforms = $dstdir + "/platforms"
$dstdir_platforminputcontexts = $dstdir + "/platforminputcontexts"

$arch = "x86-32bit"
$libicu = "/usr/lib/i386-linux-gnu"
if `uname -a` =~ /(x86_64)|(amd64)/
	$arch = "x86-64bit"
	$libicu = "/usr/lib/x86_64-linux-gnu"
elsif `uname -a` =~ /armv7l/
	$arch = "raspberrypi"
	$libicu = "/usr/lib/arm-linux-gnueabihf"
end

FileUtils.rm_rf($workdir)
FileUtils.mkpath($dstdir)

FileUtils.cp("_bin/#{$product_name}", $dstdir)
`strip #{$dstdir}/#{$product_name}`
#FileUtils.cp("#{$product_name}_ja.qm", $dstdir)

if $qt != ""

	def cp_qt_lib(name)
		libname = "lib" + name + ".so.5"
		src = $qt + "/lib/" + libname
		FileUtils.cp(src, $dstdir)
		`strip #{$dstdir}/#{libname}`
	end

	FileUtils.mkpath($dstdir_iconengines)
	FileUtils.mkpath($dstdir_imageformats)
	FileUtils.mkpath($dstdir_platforms)
	FileUtils.mkpath($dstdir_platforminputcontexts)
	cp_qt_lib("Qt5Core")
	cp_qt_lib("Qt5Gui")
	cp_qt_lib("Qt5Svg")
	cp_qt_lib("Qt5Widgets")
	cp_qt_lib("Qt5Xml")
	cp_qt_lib("Qt5Network")
	cp_qt_lib("Qt5DBus")
	cp_qt_lib("Qt5XcbQpa")

	def cp_qt_imageformat(name)
		libname = "lib" + name + ".so"
		src = $qt + "/plugins/imageformats/" + libname
		FileUtils.cp(src, $dstdir_imageformats)
		`strip #{$dstdir_imageformats}/#{libname}`
	end

	cp_qt_imageformat("qgif")
	cp_qt_imageformat("qicns")
	cp_qt_imageformat("qico")
	cp_qt_imageformat("qjpeg")
	cp_qt_imageformat("qsvg")

	def cp_qt_iconengine(name)
		libname = "lib" + name + ".so"
		src = $qt + "/plugins/iconengines/" + libname
		FileUtils.cp(src, $dstdir_iconengines)
		`strip #{$dstdir_iconengines}/#{libname}`
	end

	cp_qt_iconengine("qsvgicon")

	src = $qt + "/plugins/platforms/libqxcb.so"
	FileUtils.cp(src, $dstdir_platforms)

	src = $qt + "/plugins/platforminputcontexts/libibusplatforminputcontextplugin.so"
	FileUtils.cp(src, $dstdir_platforminputcontexts)

	def cp_libicu(name)
		src = $libicu + "/libicu" + name + ".so.52"
		FileUtils.cp(src, $dstdir)
	end

	cp_libicu("data")
	cp_libicu("i18n")
	cp_libicu("uc")

end

FileUtils.cp_r("LinuxDesktop", $dstdir)

Dir.chdir($workdir) {
	`tar zcvf #{$product_name}-#{$version_a}.#{$version_b}.#{$version_c}-linux-#{$arch}.tar.gz #{$product_name}`
}


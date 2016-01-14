require 'veygo/build/tools/cmake'

cmake = Veygo::Build::Tools::Cmake.new($C.name, $C)

task :config do
  cmake.config
end

task :build do
  cmake.build
end

task :install do
  cmake.install
end

task :clean do
  cmake.clean
end

task :rtags do
  cmake.rtags
end

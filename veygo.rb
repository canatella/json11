require 'veygo/build/cmake'

cmake = Veygo::Build::Cmake.new($C.name)

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

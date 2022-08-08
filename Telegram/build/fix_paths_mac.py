import subprocess
import os
import sys
from shutil import copyfile

executable = sys.argv[1]
app_folder = os.path.join(*executable.split('/')[:-3])
content_folder = os.path.join(app_folder, "Contents")
framework_path = os.path.join(content_folder, "Frameworks")

print(executable)
print("Working in {} ".format(app_folder))

def file_in_folder(file, folder):
    return os.path.exists(os.path.join(folder, file))

def otool(s):
    o = subprocess.Popen(['/usr/bin/otool', '-L', s], stdout=subprocess.PIPE)

    for l in o.stdout:
        l = l.decode()

        if l[0] == '\t':
            path = l.split(' ', 1)[0][1:]

            if "@executable_path" in path:
                path = path.replace("@executable_path", "")
                path = os.path.join(content_folder, path[4:])

            if "@loader_path" in path:
                path = path.replace("@loader_path", framework_path)

            if "@rpath" in path:
                path = path.replace("@rpath", framework_path)

            dependency_dylib_name = os.path.split(path)[-1]

            if "usr/local" in path:
                if app_folder in s:

                    print("Warning: {} depends on {}".format(s, path))

                    if file_in_folder(dependency_dylib_name, framework_path):
                        print("Dependent library {} is already in framework folder".format(dependency_dylib_name))
                    
                        print("Running install name tool to fix {}.".format(s))
                        
                        if dependency_dylib_name == os.path.split(s)[-1]:
                            _ = subprocess.Popen(['install_name_tool', '-id', os.path.join("@loader_path", dependency_dylib_name), s], stdout=subprocess.PIPE)

                        _ = subprocess.Popen(['install_name_tool', '-change', path, os.path.join("@loader_path", dependency_dylib_name), s], stdout=subprocess.PIPE)
                else:
                    pass

            yield path

need = set([executable])
done = set()

while need:
    needed = set(need)
    need = set()
    for f in needed:
        need.update(otool(f))
    done.update(needed)
    need.difference_update(done)

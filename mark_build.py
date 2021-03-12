import shutil
import os

# Example of running `python mark_build.py` :
#
# $ ls build/ApolloEsp32Application*                                                                                                                                           
# build/ApolloEsp32Application.bin  build/ApolloEsp32Application.elf*  build/ApolloEsp32Application.map
# $ python mark_build.py                  
# marking with ad875be
# creating build/ApolloEsp32Application_gitref_ad875be.bin
# $ ls build/ApolloEsp32Application*                                                                                                                                               
# build/ApolloEsp32Application.bin  build/ApolloEsp32Application.elf*  build/ApolloEsp32Application_gitref_ad875be.bin  build/ApolloEsp32Application.map


file_folder = 'build'
file_name = 'ApolloEsp32Application'
file_ending = '.bin'

source_path = os.path.join(file_folder, file_name + file_ending)

with open(source_path, 'rb') as f:
    f.seek(0x30)
    version_bytes = f.read(32)
    assert(len(version_bytes) == 32)
    end = version_bytes.find(b'\0')
    version = version_bytes.decode("utf-8")[:end]

    target_path = os.path.join(file_folder, f'{file_name}_gitref_{version}{file_ending}')

    print(f'marking with {version}')
    print(f'creating {target_path}')
    shutil.copyfile(source_path, target_path)

# Copyright 2020 Proyectos y Sistemas de Mantenimiento SL (eProsima).
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import argparse

from shm.clean import Clean


class Parser:
    """Shared-memory sub-commands parser."""

    __help_message='''fastdds shm [<shm-command>]\n\n
    shm-commands:\n\n
    \tclean     clean SHM zombie files
    '''

    def __init__(self, argv):
        """Parse the sub-command and dispatch to the appropiate handler.

        Shows usage if no sub-command is specified.
        """
        parser = argparse.ArgumentParser(
            usage=self.__help_message,
            add_help=True
        )

        parser.add_argument('command'
            , nargs='?'
            , help='shm-command to run'
        )

        args = parser.parse_args(argv)

        if not args.command is None:
            if args.command == 'clean':
                Clean().run()
            else:
                print('shm-command ' + args.shm_command + ' is not valid')
        else:
            parser.print_help()
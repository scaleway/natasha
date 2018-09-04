#!/usr/bin/env python
# -*- encoding: utf-8 -*-
"""
Script to generate some configuration depending on the physical platform
"""

import re
import os
import json
import argparse

from jinja2 import Environment, FileSystemLoader

from config import SETUP_FILES, CONFIG_FILE

def parse_args():
    """ Argument parser. """

    parser = argparse.ArgumentParser()
    parser.add_argument('-i',
                        '--tn-iface',
                        dest='tn_iface',
                        required=True,
                        help='TestNode physical interface used for Vlans')
    parser.add_argument('-t',
                        '--tn-mac',
                        dest='tn_mac',
                        required=True,
                        help='TestNode physical interface MAC address')
    parser.add_argument('-d',
                        '--dut-mac',
                        dest='dut_mac',
                        required=True,
                        help='DUT physical interface MAC address')
    parser.add_argument('-v', '--version',
                        action='version',
                        version='%(prog)s v1.0')

    return parser.parse_args()

def handle_config_file(env=None, filename=None, conf=None, mode=0):
    """ Render and format files """

    template = env.get_template(os.path.basename(filename))
    rendred_output = template.render(conf=conf)
    if not os.path.exists(os.path.dirname(filename)):
        os.makedirs(os.path.dirname(filename))
    with open(filename, 'w') as fid:
        fid.write(rendred_output)
    os.chmod(filename, mode)

    return

def main():
    """ Main """

    args = parse_args()
    for mac in [args.tn_mac.lower(), args.dut_mac.lower()]:
        if not re.match("[0-9a-f]{2}([:]?)[0-9a-f]{2}(\\1[0-9a-f]{2}){4}$",
                        mac):
            print 'Invalid Mac address: %s' % mac
            return

    env = Environment(loader=FileSystemLoader('templates'))

    template = env.get_template(CONFIG_FILE)
    rendred_output = template.render(tn_iface=args.tn_iface,
                                     tn_mac=args.tn_mac, dut_mac=args.dut_mac)
    # write configuration file in root directory
    with open(CONFIG_FILE, 'w') as fid:
        fid.write(rendred_output)
    os.chmod(CONFIG_FILE, 0644)

    # Load the global config from file:
    with open(CONFIG_FILE) as fid:
        conf = json.load(fid)

    # testnode and dut setup file
    for filename, mode in SETUP_FILES.iteritems():
        handle_config_file(env=env, filename=filename, conf=conf, mode=mode)

    print 'configuration files generated in "deliveries"'
    return

if __name__ == "__main__":
    main()

#!/usr/bin/env python
# -*- encoding: utf-8 -*-
"""
Script that gathers the basic tests to validate the NAT software.
"""

import argparse
import logging
import json

from config import *
from tests import *

import coloredlogs
from prettytable import PrettyTable

def run_tests(tests, log, config):
    """ Run test wrapper."""

    table = PrettyTable(['Test', 'Result'])
    for test_name in tests:
        if test_name == 'all':
            for _, conf in UNIT_TESTS.iteritems():
                test = conf['class'](conf['name'], log, config,
                                     conf['bpfilter'],
                                     count=conf['count'],
                                     payload=conf['payload'],
                                     local_sniff=conf['local_sniff'])
                result = test.run()
                table.add_row([conf['name'],
                               'Succeeded' if result else 'Failed'])
        else:
            conf = UNIT_TESTS[test_name]
            test = conf['class'](conf['name'], log, config,
                                 conf['bpfilter'],
                                 count=conf['count'],
                                 payload=conf['payload'],
                                 local_sniff=conf['local_sniff'])
            result = test.run()
            table.add_row([conf['name'],
                           'Succeeded' if result else 'Failed'])
    return table

def print_test_desc():
    """ Print available Tests with description """
    print 'Available Tests to play'
    table = PrettyTable(['Test', 'Description'])
    for _, conf in UNIT_TESTS.iteritems():
        table.add_row([conf['name'], conf['description']])

    print table

def parse_args():
    """ Argument parser. """

    test_choices = UNIT_TESTS.keys()
    parser = argparse.ArgumentParser()
    parser.add_argument('-t',
                        '--test',
                        choices=test_choices.append('all'),
                        nargs='*',
                        default='all',
                        help='The unit test list to play, default all')
    parser.add_argument('-d',
                        '--debug',
                        action='store_true',
                        help='Use Debug log level')
    parser.add_argument('-l',
                        '--list',
                        action='store_true',
                        help='Display available tests to run')
    parser.add_argument('-v', '--version',
                        action='version',
                        version='%(prog)s v1.0')

    return parser.parse_args()

def main():
    """ Main """

    args = parse_args()
    if args.list:
        return print_test_desc()

    log = logging.getLogger(__name__)
    coloredlogs.install(level='DEBUG' if args.debug else 'INFO',
                        logger=log,
                        ftm='%(asctime)s %(hostname)s %(name)s %(levelname)s %(message)s')

    with open(CONFIG_FILE, 'r') as fid:
        conf = json.load(fid)
    # Gather TestNode configuration
    conf = conf['tn']
    #print_topology()
    log.info("Running %s tests on the topology: %s" % (args.test,
                                                       print_topology()))
    result_table = run_tests(args.test, log, conf)
    log.info("TestSuite results:\n%s" % result_table)

if __name__ == '__main__':
    main()

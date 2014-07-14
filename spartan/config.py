#!/usr/bin/env python

"""
Configuration options and flags.

Options may be specified on the command line, or via a configuration
file.  Configuration files should be placed in $HOME/.config/spartan.ini

To facilitate changing options when running with nosetests, flag values
are also parsed out from the SPARTAN_OPTS environment variable.

::

    SPARTAN_OPTS='--profile_master=1 ...' nosetests



"""
import ConfigParser
import argparse
import logging
import os
import sys

import appdirs

class Flag(object):
  '''Base object for a representing a command line flag.
  
  Subclasses must implement the ``set`` operation to parse
  a flag value from a command line string.
  '''
  def __init__(self, name, default=None, help=''):
    self.name = name
    self.val = default
    self.help = help

  def __repr__(self):
    return '--%s=%s' % (self.name, self._str())

  def _str(self):
    return str(self.val)

class IntFlag(Flag):
  def parse(self, str):
    self.val = int(str)

class StrFlag(Flag):
  def parse(self, str):
    self.val = str

class BoolFlag(Flag):
  '''Boolean flag.  
  
  Accepts '0' or 'false' for false values, '1' or 'true' for true values. 
  '''
  def parse(self, str):
    str = str.lower()
    str = str.strip()

    if str == 'false' or str == '0': val = False
    elif str == 'true' or str == '1': val = True
    else: 
      assert False, 'Invalid value for boolean flag: "%s"' % str 

    self.val = val

  def _str(self):
    return str(int(self.val))

LOG_STR = {logging.DEBUG: 'DEBUG',
           logging.INFO: 'INFO',
           logging.WARN: 'WARN',
           logging.ERROR: 'ERROR',
           logging.FATAL: 'FATAL'}

class LogLevelFlag(Flag):
  def parse(self, str):
    self.val = getattr(logging, str)

  def _str(self):
    return LOG_STR[self.val]

class HostListFlag(config.Flag):
  def parse(self, str):
    hosts = []
    for host in str.split(','):
      hostname, count = host.split(':')
      hosts.append((hostname, int(count)))
    self.val = hosts

  def _str(self):
    return ','.join(['%s:%d' % (host, count) for host, count in self.val])

class AssignMode(object):
  BY_CORE = 1
  BY_NODE = 2

class AssignModeFlag(config.Flag):
  def parse(self, option_str):
    self.val = getattr(AssignMode, option_str)

  def _str(self):
    if self.val == AssignMode.BY_CORE: return 'BY_CORE'
    return 'BY_NODE'

class Flags(object):
  def __init__(self):
    from config_base import get_flag_info
    for flag in get_flag_info():
      flag_obj = globals[flag[0]](flag[1], help=flag[3])
      if flag[2] != '':
        flag_obj.parse(flag[2])
      self.add(flag_obj)

    self._parsed = False
    self._vals = {}

  def add(self, flag):
    self._vals[flag.name] = flag

  def __getattr__(self, key):
    if key.startswith('_'): return self.__dict__[key]

    assert self.__dict__['_parsed'], 'Access to flags before config.parse() called.'
    return self.__dict__['_vals'][key].val
  
  def __setattr__(self, key, value):
    if key.startswith('_'): 
      self.__dict__[key] = value
      return

    # print >>sys.stderr, ('Setting flag: %s %s', key, value)
    
    assert self.__dict__['_parsed'], 'Access to flags before config.parse() called.'
    self.__dict__['_vals'][key].val = value

  def __repr__(self):
    return ' '.join([repr(f) for f in sorted(self._vals.values())])

  def __str__(self):
    return repr(self)

  def __iter__(self):
    return iter(self._vals.items())

# print flags in sorted order
# from http://stackoverflow.com/questions/12268602/sort-argparse-help-alphabetically
from argparse import HelpFormatter
from operator import attrgetter

class SortingHelpFormatter(HelpFormatter):
  def add_arguments(self, actions):
    actions = sorted(actions, key=attrgetter('option_strings'))
    super(SortingHelpFormatter, self).add_arguments(actions)


def parse(argv):
  '''Parse configuration from flags and/or configuration file.'''

  # load flags defined in other modules (is there a better way to do this?)
  import spartan.expr.local
  import spartan.expr.optimize
  import spartan.cluster
  import spartan.worker
  import spartan.util

  if FLAGS._parsed:
    return

  FLAGS._parsed = True

  config_file = appdirs.user_data_dir('Spartan', 'rjpower.org') + '/spartan.ini'
  config_dir = os.path.dirname(config_file)
  if not os.path.exists(config_dir):
    try:
      os.makedirs(config_dir, mode=0755)
    except:
      print >>sys.stderr, 'Failed to create config directory.'

  if os.path.exists(config_file):
    print >>sys.stderr, 'Loading configuration from %s' % (config_file)

    # Prepend configuration options to the flags array so that they
    # are overridden by user flags.
    try:
      config = ConfigParser.ConfigParser()
      config.read(config_file)

      if config.has_section('flags'):
        for name, value in config.items('flags'):
          argv.insert(0, '--%s=%s' % (name, value))
    except:
      print >>sys.stderr, 'Failed to parse config file: %s' % config_file
      sys.exit(1)

  parser = argparse.ArgumentParser(
    formatter_class=SortingHelpFormatter)

  for name, flag in FLAGS:
    parser.add_argument('--' + name, type=str, help=flag.help)

  if os.getenv('SPARTAN_OPTS'):
    argv += os.getenv('SPARTAN_OPTS').split(' ')

  parsed_flags, rest = parser.parse_known_args(argv)
  for name, flag in FLAGS:
    if getattr(parsed_flags, name) is not None:
      #print >>sys.stderr, 'Parsing: %s : %s' % (name, getattr(parsed_flags, name))
      flag.parse(getattr(parsed_flags, name))

  # reset loggers so that basic config works
  logging.root = logging.RootLogger(logging.WARNING)
  logging.basicConfig(
    format='%(created)f %(hostname)s:%(pid)s %(filename)s:%(lineno)s [%(funcName)s] %(message)s',
    level=FLAGS.log_level,
    stream=sys.stderr)

  for f in rest:
    if f.startswith('-'):
      print >>sys.stderr, '>>> Unknown flag: %s (ignored)' % f

  if FLAGS.print_options:
    print 'Configuration status:'
    for name, flag in sorted(FLAGS):
      print '  >> ', name, '\t', flag.val

  return rest


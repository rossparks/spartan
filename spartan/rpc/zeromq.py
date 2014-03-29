'''ZeroMQ socket implementation.'''
import cProfile
import collections
import os
import threading
import socket
import traceback
import sys
import fcntl
import time
import zmq
from .common import Group
from spartan import util
from zmq.eventloop import zmqstream, ioloop 
from rlock import FastRLock

#for client socket, we have one polling loop per thread.
_loop = threading.local()

def get_threadlocal_loop():
  if not hasattr(_loop, "val"):
    _loop.val = ioloop.IOLoop()
  return _loop.val

class ZMQServerLoop(object):
  '''  
  A subset functions of tornado.ioloop, but used only for ONE 
  server socket to send data and receive data in polling thread.    
  '''
  def __init__(self, socket):
    self._poller = zmq.Poller()
    self._running = False
    self._running_thread = None
    self._socket = socket
    self._direction = zmq.POLLIN 
    self._pipe = os.pipe()
    fcntl.fcntl(self._pipe[0], fcntl.F_SETFL, os.O_NONBLOCK)
    fcntl.fcntl(self._pipe[1], fcntl.F_SETFL, os.O_NONBLOCK)
    self._poller.register(self._pipe[0], zmq.POLLIN)
    self._poller.register(socket.zmq(), self._direction)

  def start(self):
    MAX_TIMEOUT = 10 
    self._running = True
    # Record which thread this loop is running on. The Server Socket can check this
    # to decide what to do.
    self._running_thread = threading.current_thread()
    _poll_time = 1
    _poll = self._poller.poll
    socket = self._socket 
    
    while self._running:
      socks = dict(_poll())
      if len(socks) == 0:
        _poll_time = min(_poll_time * 2, MAX_TIMEOUT)
      else:
        _poll_time = 1 

      for fd, event in socks.iteritems():
        if fd == self._pipe[0]:
          os.read(fd, 10000)
          continue
        
        if event & zmq.POLLIN: 
          socket.handle_read()
        if event & zmq.POLLOUT: 
          socket.handle_write()
      self._poller.register(socket.zmq(), self._direction)
    
    # Close serversocket after the loop ends.
    self._socket.close()

  def stop(self):
    self._running = False  
    self.wakeup()
     
  def running_thread(self):
    ''' query the running thread '''
    return self._running_thread 

  def modify(self, direction):
    self._direction = direction
    self.wakeup()

  def wakeup(self):
    os.write(self._pipe[1], 'x')

class Socket(object):
  def __init__(self, ctx, sock_type, hostport, event_loop=None):
    self._zmq = ctx.socket(sock_type)
    self.addr = hostport
    self._event_loop = event_loop or get_threadlocal_loop() 
    self._stream = zmqstream.ZMQStream(self._zmq, self._event_loop)
    #Register this socket with the ZMQ stream to handle incoming messages.
    self._stream.on_recv(self.on_recv)

  def zmq(self):
    return self._zmq
    
  def __repr__(self):
    return 'Socket(%s)' % ((self.addr,))
  
  def close(self, *args):
    self._zmq.close()

  def send(self, msg):
    if isinstance(msg, Group):
      self._zmq.send_multipart(msg, copy=False)
    else:
      self._zmq.send(msg, copy=False)

  def on_recv(self, msg):
    self._handler(msg[0])

  def connect(self):
    self._zmq.connect('tcp://%s:%s' % self.addr)

  @property
  def port(self):
    return self.addr[1]

  @property
  def host(self):
    return self.addr[0]
  
  def register_handler(self, handler):
    self._handler = handler

class ServerSocket(Socket):
  ''' ServerSocket use its own loop and use its own handle_read/handle_write functions. '''
  def __init__(self, ctx, sock_type, hostport):
    #Socket.__init__(self, ctx, sock_type, hostport, event_loop)
    self._zmq = ctx.socket(sock_type)
    self._listen = False
    self.addr = hostport
    self._event_loop = ZMQServerLoop(self)
    self._out = collections.deque() 
    self._out_lock = FastRLock()
    self.bind()

  def listen(self):
    self._listen = True
  
  def handle_read(self):
    if self.listen == False:
      return
    packet = self._zmq.recv_multipart(copy=False, track=False)
    source, rest = packet[0], packet[1:]
    stub_socket = StubSocket(source, self, rest)
    self._handler(stub_socket)

  def handle_write(self):
    ''' 
    Thiss function is called from inside of the poll thread.
    It sends out any messages which were enqueued by other threads. 
    '''
    with self._out_lock:
      while self._out:
        msg = self._out.popleft()
        if isinstance(msg, Group):
          self._zmq.send_multipart(msg, copy=False)
        else:
          self._zmq.send(msg, copy=False)
      self._event_loop.modify(zmq.POLLIN)

  def send(self, msg):
    # We put the msg in queue and let the polling thread send the message.
    # An alternative way is that we send the message directly if it is in
    # polling thread, otherwise we put the message in queue.
    with self._out_lock:
      self._out.append(msg)
      self._event_loop.modify(zmq.POLLIN | zmq.POLLOUT)

  def bind(self):
    host, port = self.addr
    host = socket.gethostbyname(host)
    util.log_debug('Binding... %s', (host, port))
    if port == -1:
      self.addr = (host, self._zmq.bind_to_random_port('tcp://%s' % host))
    else:
      try:
        self._zmq.bind('tcp://%s:%d' % (host, port))
      except zmq.ZMQError:
        util.log_info('Failed to bind (%s, %d)' % (host, port))
        raise
    

class StubSocket(object):
  '''Handles a single read from a client'''
  def __init__(self, source, socket, data):
    self._out = collections.deque()
    self.source = source
    self.socket = socket
    assert isinstance(data, list)
    assert len(data) == 1
    self.data = data[0]

  @property
  def addr(self):
    return self.socket.addr

  def recv(self):
    assert self.data is not None, 'Tried to read twice from stub socket.'
    res = self.data
    self.data = None
    return res

  def send(self, req):
    if isinstance(req, Group):
      req = Group([self.source] + list(req))
    else:
      req = Group((self.source, req))
    self.socket.send(req)


def server_socket(addr):
  host, port = addr
  return ServerSocket(zmq.Context.instance(), zmq.ROUTER, (host, port))

def server_socket_random_port(host):
  return ServerSocket(zmq.Context.instance(), 
                      zmq.ROUTER, 
                      (host, -1))

def client_socket(addr):
  host, port = addr
  return Socket(zmq.Context.instance(), zmq.DEALER, (host, port))

## Running the Autograder

**Bind Errors**
Make sure all addresses are free
```
sudo netstat -nlp
```
Find the 10.1.1.* addresses in use then run
```
sudo kill <pid>
```

**Subprocess OS errors**
Make sure `manager/manager_send` is executable

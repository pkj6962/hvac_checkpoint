msg=""
for str in $@
do
   msg="${msg} ${str}" 
done
echo $msg

git add * 
git commit -m $msg
git push origin master

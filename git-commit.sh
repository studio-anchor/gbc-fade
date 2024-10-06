# . git-commit.sh 

export NOW=$(date)

git add .
git commit -m "$NOW"
git push origin master
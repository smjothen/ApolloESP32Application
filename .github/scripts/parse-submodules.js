const parse = require('parse-git-config');
 
// sync
console.log(parse.sync({ path: '.gitmodules' }));
 
// using async/await
// (async () => console.log(await parse({ path: '.gitmodules' })))();